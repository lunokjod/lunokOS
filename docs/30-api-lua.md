# 30 · API de Lua

Aquest és el contracte entre una app i el sistema. Tot el que no hi és, no existeix.

## Com es llegeix

- **Síncrona** vol dir que torna de seguida i no espera res.
- **⏳ asíncrona** vol dir que **retorna una promesa** i que el nucli no s'atura mentre la cosa
  passa. El resultat s'agafa amb `:await()` o amb `:then(...)`.
- **Error immediat**: la funció llança una taula `{code, message}`. **Error asíncron**: la promesa
  es rebutja amb la mateixa taula.
- Els mòduls són **globals plans** (`sys`, `time`, `alarm`…). No hi ha cap taula `lunok.` que els
  envolti: ocupa més memòria i no aporta res.

### `require(name)`: una app en més d'un fitxer (0.148.0)

`require('shelf')` llegeix **`shelf.lua` de la carpeta on es va instal·lar l'app** i el corre **una vegada**;
el valor que torna és el mateix a cada `require` següent amb aquell nom, i un mòdul que no torna res val
`true`. El que queda carregat és a **`package.loaded`**, que és l'única cosa que té la taula `package` —
**no hi ha `package.path`, ni `cpath`, ni searchers, ni preload**—.

- **El nom**: `require('pages/chapter')` és `pages/chapter.lua`, dins de la mateixa app. No pot pujar
  (`..`), no pot començar per `/` i **no porta punts**; el nucli és qui ho comprova, perquè és qui sap on és
  l'app (docs/70).
- **El mòdul no és cap porta al disc**: només arriba a la carpeta d'aquella app, i `load`, `loadfile` i
  `dofile` segueixen sent `nil` (docs/20).
- **El motiu és la mida**: cada mòdul és un chunk amb el seu sostre de codi i els seus 200 locals, o sigui
  que una app de mil cinc-centes línies pot créixer (docs/46, D6).
- **Errors**: un nom que no és cap fitxer, un nom que surt de l'app i un mòdul que peta són un **error de
  Lua** (es poden agafar amb `pcall`); el d'un mòdul que peta diu el nom del fitxer i la línia
  (`part.lua:1: boom`).

```lua
local shelf = require('shelf')     -- llegeix shelf.lua i el desa a package.loaded.shelf
local again = require('shelf')     -- el mateix valor, sense tornar a llegir res
assert(shelf == again)
```

## Què hi ha avui (30/10)

El contracte d'aquest document és el sencer; el codi encara no. Ja hi són:

- `print(...)`, `console.print(...)` i `console.write(s)`.
- `sys.name`, `sys.version`, `sys.board`, `sys.emulator`, `sys.lua` (valors, sense cridar res).
- `sys.heap()`, `sys.gc(mode)`, `sys.time_ms()`, `sys.turn()`, **`sys.delay(ms)`** i **`sys.caps()`**.
- **`console.read_line()`** i, amb ella, **el teclat de l'app**: mentre una app viu, tot el que s'escriu va a ella i no a la shell.
- **`term`**: `clear`, `reset`, `color`, `move`, `hide_cursor`, `show_cursor`, `width`, `height` i les constants. Si la consola no fa ANSI, no fan res i no donen error.
- **`Promise`**: `p:await()`, `p:and_then(ok[, err])`, `p:catch(err)`, `p:finally(fn)`,
  `p:timeout(ms)`, `p.state`, i `Promise.resolve/reject/is`. Errors `E_NOTASK`, `E_TIMEOUT`,
  `E_INVAL` i `E_BUSY` amb taula `{code, message}`.
- **`net`** (connectar, escanejar, afegir, esborrar, provisioning i `ntp`), **`http.get`** (una
  pàgina amb TLS), **`fs`**, **`qr`**, **`json`**, **`time`**, **`alarm`**, **`term`**, **`app`**,
  **`power`** (les dues menes de son, la velocitat del processador i la bateria), **`led`** (el llum),
  **`imu`** (el sensor del Matrix) i **`button`** (el botó, i el hook `on_button`).

**Encara no hi és**, i una app que ho cridi peta amb «attempt to call a nil value»:
`console.flush()`, `console.read()`, `console.on_data()`, `time.format`/`parse`/`set`/`sync`,
`Promise.all`/`race`, `app.spawn`/`raise`/`save`/`restore`, `net.softap`, `power.off`, i els mòduls
`store` i `log`. (`console.read_byte`, `time.utc` i `time.tz` hi són des de la 0.125.0 i ja no són en
aquesta llista; `time.local` no hi serà mai, perquè `local` és una paraula de Lua i la crida es diu
`time.date`.) Les taules d'aquest document ho marquen amb
⏳, i el que queda fitat és a `docs/90`.

`term.width()` i `term.height()` tornen **80 i 24**, que és el valor per defecte del contracte: el
fitxer `cfg/console.json` encara no es llegeix.

## Errors

| Codi | Quan |
|---|---|
| `E_UNSUPPORTED` | La placa no té la capacitat que demana la crida |
| `E_NOENT` | El camí o la clau no existeix |
| `E_EXIST` | **Reservat**: cap crida el torna avui (el que hi ha és `E_NOENT` quan falta i el refús de política de `docs/70` quan no toca) |
| `E_NO_HANDLES` | El pool de fitxers oberts és ple |
| `E_INVAL` | Paràmetre dolent |
| `E_TIMEOUT` | Ha trigat més del que s'havia dit |
| `E_IO` | Error del disc o del port |
| `E_NOMEM` | S'ha exhaurit el pressupost de memòria de l'app |
| `E_PERM` | El manifest no declara el permís |
| `E_TOOBIG` | El resultat no cap al sostre permes |
| `E_NOSPC` | No queda lloc: el disc o la memòria per a la resposta |
| `E_UNAVAIL` | El que es demana no hi és en aquesta placa ara (per exemple, una targeta que no s'ha muntat) |
| `E_TLS` | El certificat del servidor no és acceptat, o la connexió xifrada no s'ha pogut fer |
| `E_HTTP` | El servidor no hi era, o ha plegat abans d'acabar de respondre |
| `E_NONET` | La placa no és en cap xarxa: la ràdio no ha pujat, o s'ha perdut |
| `E_BUSY` | Ja hi ha una operació en curs en aquest recurs, o el **llum és ocupat** amb un moment del sistema (docs/30) |
| *(les paraules del maquinari)* | Des de la 0.118.10, quan el que ha refusat és un driver i el driver ho sap, **el codi no és `E_BUSY` sinó la seva frase sencera**, amb el nom de l'error del framework i la memòria lliure: `the radio did not come up (ESP_ERR_NO_MEM; 4 120 B free, largest 1 536 B)`. `E_BUSY` queda per al cas de debò —una ràdio ocupada amb una altra feina—, i la resta es poden distingir sense un flaix més (docs/50) |
| `E_NOTASK` | S'ha cridat `await` fora d'una tasca |
| `E_NOTFOREGROUND` | **Reservat**: cap crida el torna avui. Dins d'una finestra d'alarma el que passa és que `console.is_foreground()` diu `false` i el que s'escriu a la consola queda al log (docs/60) |
| `E_BUDGET` | **Reservat**: el pressupost per torn ja existeix i fa cedir l'app (0.90.0), però el matar amb aquest codi després de dos avisos encara no (docs/20, docs/75) |

## Capacitats

`sys.caps()` torna una taula plana. Cada clau és `true` o `false`:

`console`, `ansi`, `display`, `input`, `led`, `imu`, `env`, `littlefs`, `sd`, `nvs`, `rtc`, `rtc_wake`,
`net_wifi`, `net_http`, `net_prov`, `battery`, `sleep_light`, `sleep_deep`.

`rtc` és el **domini RTC del xip** —el temporitzador i la memòria que sobreviu al son profund—, no un
rellotge amb pila: dona l'hora mentre hi ha corrent i **la perd en desconnectar**. `input` i `led` són
de la placa, i per això cap xip els té per si sol.

La regla de la llista, perquè no s'hi busqui el que no hi ha:

- **Una entrada és una pregunta que l'app fa abans de triar camí**, i la resposta és el que li diu si
  ha de fer una cosa o una altra. Quan la resposta és que no, la crida no falla d'una manera estranya:
  dona `E_UNSUPPORTED`.
- **Hi ha interfícies i hi ha propietats d'una interfície.** `console`, `display`, `input`, `led`,
  `imu`, `env`, `littlefs`, `sd`, `nvs`, `rtc` i `net_wifi` són interfícies (una peça que pot ser
  `nullptr`); `rtc_wake`, `sleep_light`, `sleep_deep` i `battery` són propietats de les del rellotge i
  de l'energia, i `net_http` i `net_prov` de la de xarxa. `ansi` és l'única que no és maquinari: és si
  el terminal de l'altra banda entén els colors.
- **Els números i les formes no són capacitats, són dades**: quantes tecles hi ha i de quina mena és
  `button.list()`; quants llums, `led.count()`; quants bytes queden, `fs.space()` i `sys.heap()`. Una
  capacitat que digués «tres tecles» seria una segona veritat que es pot desdir de la primera.
- **Per tant, les tecles hi són sota `input`** (una tecla o un dial de tres dona el mateix `input:
  yes`) i la **memòria del RTC hi és sota `rtc`**, amb el fitxer que la fa servir a `mounts` (`/rtc`,
  7 168 B). **La FRAM és l'única peça que no hi és**: només es veu com a `/fram` a `mounts`, mentre
  que `/sd`, `/nvs` i `/rtc` tenen bit **i** muntatge. És una decisió oberta a `docs/90`.

Als ATOM d'avui `caps` respon **13 de 19**, el Matrix **14 de 19** perquè hi afegeix `imu`, el Paper
**15 de 19** amb el SHT30 contestant, el vidre i el GT911, i sense bateria ni targeta —**16** amb la
bateria llegida **o** la targeta, **17** amb totes dues, que és el sostre d'aquesta placa: les deu del
xip, `littlefs`, `sd`, `input`, `battery`, `env`, `touch` i `display`— i l'emulador **el que diu el
model que imita**: 13 al model de l'ATOM (12 si el terminal no és ANSI), 14 al del Matrix i **17** al
del Paper (16 sense ANSI), que ja porta la targeta `emulator/sd`, el vidre, el GT911 i la bateria que el
emulador es presta sol, i els treu amb `--no-display` / `--no-touch` (docs/50, docs/85). Els números del
Paper **no són una previsió**: l'1/11 la placa va dir **15 de 18** amb la 0.10.1 (bateria i targeta
dins) i una lectura de l'aire hi va donar `air 21.1 C 63.5 %` (docs/95, E-5).

---

## `sys`

`sys` és una taula plana: el que és un valor es llegeix sense cridar res, i el que fa feina és una
funció.

| Nom | Torna | Notes |
|---|---|---|
| `sys.name` | cadena | `"lunokOS"` |
| `sys.version` | cadena | La versió del sistema, la mateixa que respon `version` |
| `sys.board` | cadena | El nom de la placa **tal com surt a `board =` de `platformio.ini`** (`m5stack-atom`), o el nom propi de la placa de la família que canvia què es pot tocar (`m5stack-atom-matrix`). Al PC és el model que l'emulador ha rebut a `--board` (docs/85) |
| `sys.emulator` | booleà | Cert només al PC: diu que la placa de `sys.board` és de mentida |
| `sys.lua` | cadena | La versió de Lua que porta el sistema |
| `sys.caps()` | taula de capacitats | Vegeu a dalt |
| `sys.heap()` | `{free, used, limit, peak, extra_total, extra_free, ram_free, ram_largest}` | Bytes del pressupost de l'app i, al darrere, la **memòria extra** de la placa (la PSRAM del Paper): `extra_total` és el que la CPU **pot adreçar** (4 MB, no els 8 del full de dades) i `extra_free` el que en queda; tots dos són **zero** en una placa que no en té, que és la veritat i no un error. **`ram_free` i `ram_largest` són la RAM de la placa** (l'altra casa, la que necessiten els drivers i el sistema de fitxers) i no la suma amb la PSRAM: és el número que va explicar un `ENOMEM` de `fopen` amb 1,7 MB de PSRAM lliures, i el terra `pal::kHomeReserveBytes` és el que el manté (0.151.20, docs/75) |
| `sys.gc("step")` | — | Fa un pas de col·lector |
| `sys.gc("full")` | — | Col·lecció completa (cara; no es fa servir enmig d'un dibuix) |
| `sys.gc("count")` | enter | KB en ús |
| `sys.delay(ms)` ⏳ | promesa | Espera sense tocar la cua d'alarmes ni el port sèrie |
| `sys.time_ms()` | enter | Rellotge monòton en mil·lisegons. **Mai salta enrere** |
| `sys.turn()` | enter | **Quantes instruccions val un torn de l'app** (0.174.7): el pressupost que el comptador de l'intèrpret compara, que és un ajust de la placa —`cfg turn <n>`— i no una constant. Una app que raoni sobre torns (`luaperf` en mesura el preu) el pregunta aquí i no se'n copia el número, perquè qui el canvia és una persona al prompt (docs/20, docs/75) |
| `sys.session(key, bytes)` | **un objecte** o `nil, error` | **Una memòria de sessió, que el nucli guarda per a aquesta app sota aquesta clau** (0.179.0): la demana una vegada i la troba cada cop que l'app torna a arrencar —fins que la placa s'apaga o reinicia, o fins que falta memòria i el sistema la dóna—; les claus són **de l'app i no del sistema** (`"apps"` és un bloc per al llançador i un altre per a l'app que vingui després), el bloc és **a zero** el primer cop (així una app distingeix una caché freda d'una de calenta) i pot ser més petit del que s'ha demanat: `size()` diu el que té de debò. `read(offset, n)`, `write(offset, text)` —que torna els bytes que hi han cabut—, `clear()`, `size()`, **`release()`** (0.179.3, l'app dóna el bloc abans d'acabar) i **`alive()`** (0.179.8, i és la pregunta que fa un bloc quan el sistema se l'ha pogut quedar: `true` mentre sigui el bloc que el nucli va donar, `false` des que el relleu de l'allocator l'ha donat per falta de memòria; les altres crides d'un bloc que se n'ha anat aixequen `the session memory was given back` i no llegeixen res, i una app que pregunti abans d'usar-lo i en demani un altre quan la resposta és `false` no perd la pàgina, docs/75); `nil, E_NOMEM` vol dir que no hi ha lloc i l'app segueix sense caché. **I cada moviment seu surt al registre** (0.179.3): `[I][MEMORY] home_eink asks for 'apps': 32768 B, made`, `… and it has them`, `… grown from 4096`, `[I][MEMORY] hog gives back 'hog': 4096 B` i, quan és el sistema qui el pren, `[I][MEMORY] … the memory is short` amb `[W][MEMORY] the memory is short: 2 block(s), 1081344 B given back for an ask of 1024 B` al darrere —**el sistema només hi va quan una petició ha fallat**, i el bloc que la petició està movent es queda: `[I][MEMORY] home_eink keeps 'apps': 32768 B, it is the block that asked` (0.179.6, docs/75) |
| `sys.reboot([reason])` | no torna | **Reinicia la placa** (0.171.0): la imatge del moment (el dibuix `reboot` del splash) va al vidre, s'espera la tinta i la placa torna a arrencar. `reason` és el que queda al log —`[I][APPS] wipe restarts the board: the NVS was erased`—, i el que fa la feina és el mateix `reboot` del prompt (docs/50). **No torna**: la VM de l'app mor amb la placa, i per això el que calgui desar s'ha de desar abans |
| `sys.shell()` | objecte de sessió o `nil, error` | **Una sessió del shell del sistema** (0.159.0): el mateix shell que atén el prompt, amb **tres canonades** en comptes d'un terminal. Vegeu «Una sessió del shell» més avall |

## Una sessió del shell (0.159.0)

**El shell és del sistema i no del fil** on li toca parlar. `sys.shell()` en demana una sessió i torna un objecte
amb les tres canonades d'una —`stdin`, `stdout` i `stderr`—, i el shell que hi ha a sota és el mateix que atén el
prompt: la línia que s'escriu al primer és una comanda que el sistema executa, el que contesta surt pel segon i el
que es queixa pel tercer. Sense consola al mig, **sense prompt i sense eco**: una canonada no és un terminal, i el
que llegeix l'app és el que la comanda ha dit i res més (docs/75).

```lua
local sh = sys.shell()
if sh == nil then
  console.print('sense sessió')
  return
end

sh.stdin:write('version\n')          -- una comanda per línia, com al prompt
local said = sh.stdout:read()        -- el que ha contestat (cadena; buida vol dir res encara)
sh.stdin:write('no_existeix\n')
local why = sh.stderr:read()         -- el que s'ha queixat, pel seu costat
sh:close()                           -- o escriure 'exit\n' al stdin, que és com es deixa un shell
```

- **`stdin:write(text)`** → `true` si el shell l'ha pres. Una línia acaba amb `\n`; un `\r` s'ignora, perquè un
  terminal n'envia dos i una canonada un. Una línia de més de **127 bytes** es rebutja i es diu pel `stderr`.
- **`stdout:read([màxim])`** i **`stderr:read([màxim])`** → el que hi ha ara mateix, fins al màxim (1 024 B com a
  sostre); una cadena buida és una canonada buida i no el final de res.
- **`sh:dropped()`** → quants bytes s'han perdut perquè ningú llegia. És el número que diu que una app s'ha
  quedat curta de lectura; **el shell no s'atura mai per una canonada plena**.
- **`sh:close()`** o un `exit`/`logout` al `stdin`: la sessió s'acaba i les canonades es tornen. El `__gc` ho fa
  igual si l'app s'oblida.
- **`lua` no es pot demanar des d'una sessió**: l'intèrpret és del prompt de la consola i pinta una vista seva; el
  shell ho diu i no ho fa. `run` tampoc, perquè una app ja és al davant quan demana la sessió.
- **El que costa**: 1 024 B de respostes i 256 B de queixes **demanats al heap** mentre la sessió viu, i res quan es
  tanca. **Una sessió alhora**: la segona rep `E_BUSY` amb el motiu, i no una cua que ningú buida (docs/75).

## L'app `shell`: el terminal amb pestanyes (0.162.0)

**`apps/shell` és el shell del sistema al vidre i no una segona implementació**: cada pestanya és una
sessió de `sys.shell()` i l'app només fa de pàgina. La composició és la d'un terminal —**la barra de
menú a dalt, les pestanyes a sota i la caixa negra amb lletra blanca** la resta—, i per això no
s'assembla a cap document del sistema: aquí no es llegeix, aquí s'escriu.

| La peça | Què fa |
|---|---|
| `ui.menu_bar`, títol **File** | **New** obre una sessió (i una pestanya) i **Close** tanca la que és al davant. És `on_pick(self, title, entry)` qui ho rep |
| `ui.tabs` amb `closeable = true` | una pestanya per sessió, amb una **creu a la dreta** que el sistema dibuixa i que arriba com a `on_close(self, index)`; el cos de la pestanya arriba com a `on_change(self, index)` i el dit, per `tabs:tap(x, y)` |
| `ui.textbox_multiline` amb `color = 15` i `background = 0` | el full del terminal. **La lletra blanca sobre negre és una decisió de la pàgina**, i el color 0 no vol dir «sense color» sinó negre |
| `session.stdin:write` / `stdout:read` | el que una persona escriu a l'última línia se'n va a la sessió; el que la sessió contesta s'afegeix al text de la pestanya |

- **La caixa és de només lectura i la línia del prompt no ho és.** Una pàgina que es llegeix no ha
  de poder-se tocar, però l'última línia és on s'escriu, i el que decideix és **el cursor**:
  l'app mira `box.cursor` a cada torn i posa `box.readonly = false` (i `box.keyboard = true`) quan és
  a l'última línia. Aquesta regla viu a l'app i no al control perquè **la caixa de diverses línies no
  té ganxo de cursor mogut** —`on_change` només arriba quan canvia el text—; si algun dia el té, el
  lloc d'aquesta regla és el control.
- **Una línia que acaba en salt de línia és una comanda** i surt del full: el shell és qui l'ecoïa, i
  la resposta torna per `stdout` com tota la resta. Una línia que no acaba en salt es queda a la
  pàgina, que és el que fa un terminal mentre s'escriu.
- **La creu és una promesa**: `on_close` crida `session:close()` abans de treure la pestanya. Una
  pestanya que marxa amb la sessió oberta deixa una sessió que ningú no llegeix (i la segona sessió
  rep `E_BUSY`, o sigui que la pàgina es quedaria sense cap pestanya nova possible).

## Saber on s'executa

Una app pot acabar corrent en una placa que no ha vist mai. `sys.board` és l'única cosa que li diu
on és, i és **el nom que la placa té a `platformio.ini`** (`board = m5stack-atom`), no cap àlies
inventat pel sistema. Al PC no hi ha cap placa ni cap línia `board =`, i la manera de no inventar-ne
cap nom és dir quin model fa l'emulador: `sys.board` respon aquest model (l'ATOM si no es diu res) i
`sys.emulator` diu, a part, que el maquinari de sota és fals.

| `sys.board` | Què hi ha | Què no hi ha |
|---|---|---|
| `m5stack-atom` | consola, LittleFS, NVS, ràdio Wi-Fi amb TLS, provisioning, son, un llum, un botó, un port Grove I2C | pantalla, bateria, teclat, targeta SD, sensor inercial |
| `m5stack-atom-matrix` | el mateix, amb **vint-i-cinc llums** i un **sensor inercial** | el mateix que el Lite |
| `m5stack-paper` | el mateix que el Lite pel que fa al xip (consola, disc, NVS, ràdio amb TLS, provisioning, son), amb **tres tecles**, **targeta SD** al slot (a `/sd`) i sense cap llum | pantalla, bateria, toc i sensor, encara (docs/90) |
| `ttgo-t-koala` | el mateix que el Lite pel que fa al xip, amb **4 MB de PSRAM mapejada** (8 MB al xip), **targeta SD** a `/sd` i un **LED verd** a GPIO5 | pantalla, tàctil, sensor, tecles, indicador de bateria i apagar-se, encara (docs/50, docs/95) |

Al PC la taula d'un model és la de la placa **amb les peces falses a sobre** (docs/85): l'emulador té el
dial de tecles, el panell i el sensor alhora perquè el que no hi és no es pot provar. Aquí és on entra
`sys.emulator`, que és **cert només al PC** i no és el mateix que preguntar `sys.board == "emulator"`
(que ja no passa mai): el nom diu on ets, el booleà diu que el maquinari és de mentida.

Com s'ha de fer servir: **mirar primer i decidir després**, mai suposar. Una app que dibuixa a una
pantalla que potser no hi és:

```lua
if sys.board == "m5stack-atom" then
  -- el camí pensat per a aquesta placa
elseif sys.emulator then
  -- al PC no hi ha maquinari de debò: es prova la lògica i prou
end
```

Dues regles:

- **La comprovació grossa és el nom de la placa; la fina és `sys.caps()`**, que diu què té de debò
  aquesta banda. El nom serveix per als casos en què canvia **la manera** de fer servir el maquinari,
  no només si hi és o no.
- **Un nom desconegut no es pot ignorar.** Si `sys.board` no és cap dels que l'app entén, ho diu i
  surt; no es queda provant maquinari a cegues.

### `ui`, `canvas`, `keyboard`, `font` i `epub` **no existeixen** en una placa sense pantalla (0.179.9)

**A les quatre plaques sense vidre —els dos ATOM, l'ESP32 de DevKit i el T-Koala— aquestes cinc taules no hi són, i no
hi són perquè el seu codi no s'ha compilat**: la pàgina sencera, el lector de llibres, les cares de la lletra i les
llibreries que els donen suport (`epub`, `zip`, `inflate`, `ttf` i `img`) queden fora del build amb un sol
`LUNOK_GLASS 0` de la identitat de la placa (docs/50, docs/75). Una app que les toqui no falla a mig fer: peta a la
primera línia amb `attempt to index a nil value (global 'ui')`, que és el mateix que dir que en aquesta placa no hi ha
pàgina.

```lua
-- i per tant això no és una app d'aquest sistema sense una comprovació abans
if not sys.caps().display then
  console.print('aquest build no porta pàgina')
  return
end
```

**El que sí que hi és sempre**: `sys`, `fs`, `net`, `http`, `json`, `qr` (un QR s'imprimeix en text i una placa sense
pantalla el necessita per al provisioning), `button`, `led`, `power`, `imu`, `env`, `alarm`, `time`, `apps`, `app`,
`require` i la consola. La frontera és el **vidre** i no «el que dibuixa»: una placa amb pantalla i sense tàctil té `ui` i
no té dit, i una sense pantalla no té cap de les dues (docs/50).

## `apps`

**La porta de les apps, per a un llançador** (0.153.0, i 0.154.0 amb els noms i el manifest separats). El sistema és qui
sap què hi ha al disc —`/apps` és la porta de `kernel::apps`, amb els dos discs junts (docs/40)—, i una app que ho
llegís caminant `/apps` seria un segon lector del mateix fitxer.

| Crida | Torna | Notes |
|---|---|---|
| `apps.list()` | taula de cadenes | **Els noms de la porta i res més**: un `id` per app, en l'ordre que el disc els dona. **No obre cap manifest**, o sigui que és el que un llançador pot ensenyar de seguida —el nom amb una icona genèrica— i el que arriba abans que el disc tregui res |
| `apps.info(id)` | taula o `nil, error` | **El manifest d'aquella app**, llegit quan l'app ho demana: `{id, title, icon, launcher, card, works_here}`. `icon` és **el camí que el disc contesta** (`/apps/<id>/res/icon.png`), o `""` si el manifest no en nomena cap; `launcher` és el `"launcher": true`; `card` diu de quin disc ha sortit i `works_here` si aquesta placa la pot arrencar. El no diu per què (`E_INVAL`): un nom que la porta no té, o un manifest que aquest sistema no llegeix |

**Per què són dues crides i no una** (0.154.0): una pàgina que espera tots els manifests i totes les imatges abans
d'ensenyar res arriba tard, i una app lenta o un manifest trencat atura les altres. Amb les dues, el llançador dibuixa
els noms de cop i va demanant el manifest d'una app per torn: cada cel·la passa del nom del disc i una lletra a la icona i
el nom de debò, i les que no es poden llegir queden amb el que se'n sap.

**El que no hi és**: les apps d'una altra placa **surt a la llista** (una app que hi és i no es pot arrencar és una cosa que
una persona ha de poder veure i entendre, i `apps.info` ho diu amb `works_here`), i l'app que és un llançador **surt**
amb `launcher` a `true`: el filtre és de qui dibuixa la graella, no de la porta. `home_eink` és el llançador d'aquest
sistema i és el primer client d'aquestes dues crides.

**I el llançador no les fa totes cada cop** (0.179.0): `home_eink` guarda els manifests i les icones ja descodificades
en una **memòria de sessió** (`sys.session('apps', …)`, més avall) i, quan torna —que en una placa de tres tecles és cada
cop que una app s'acaba—, la pàgina és sencera **al primer torn**: el log diu `home: cache warm, 10 apps, 9 pictures` i no
hi ha cap `home: app`, perquè la cua de manifests és buida i cada icona és un `cv:blit` d'un bloc i no una lectura ni una
descodificació.

**I la porta es pregunta després de la pàgina, no abans** (0.179.1): amb un bloc calent el llançador dibuixa la graella
**sense llegir res** i és el **torn següent** el que demana `apps.list()`; si la llista és la de la pàgina no passa res, i si
una app s'ha instal·lat o desinstal·lat en aquesta sessió la pàgina es torna a fer amb la llista de la porta i només es
demana el manifest de les que són noves. El log d'un retorn diu, en aquest ordre, `home: cache warm, …`, `home: 10 cells`
—la pàgina, del bloc—, `home: 10 ids: …` —la porta, que hi està d'acord— i cap `home: app`; i el d'una app instal·lada
diu `home: 11 ids`, `home: 11 cells` i `home: app newone` (mesurat a l'emulador amb una carpeta posada a la targeta
mentre corria).

## `app`

| Crida | Torna | Notes |
|---|---|---|
| `app.id` | cadena | El nom del directori de l'app |
| `app.args` | taula o `nil` | `{path, ext, source}` quan l'app s'obre per obrir un fitxer, o quan el prompt la hi apunta: `run editor res/notes.txt` i `open txt res/notes.txt` són el mateix per a l'app. **Des de la 0.120.0 hi ha també `flags`** quan el manifest declara `opensArgs`: la línia de commutadors de l'app (`-ro`), tal com està escrita i sense que el sistema en llegeixi res |
| `app.info()` | taula | `{id, title, version, opens}` de l'app: el que el nucli en guarda, **no** el manifest sencer. **`opens` és la llista sencera** que el manifest declara, amb els noms separats per espais —`"txt md lua"`—; una app que no n'obre cap rep `""` (0.120.0) |
| `app.spawn(fn)` ⏳ | promesa | Crea una tasca; la promesa es resol amb el que torni `fn` |
| `app.exit([code])` | no torna | Tanca l'app; `code` per defecte `0`, i la shell diu `app exit N` si no és zero |
| `app.run(id[, back])` | `true`, o `nil, error` | **Arrenca una altra app** (0.153.0): l'app que hi ha davant es tanca amb els seus comiats i la del `id` ocupa el seu lloc. És **un desig i no un canvi immediat** —qui pregunta és la mateixa app que el intèrpret té a sota, i tancar-la dins la crida destruiria l'estat on la crida corre—: el nucli el porta a terme al final del torn, i fins aleshores `app.run` ha contestat `true` i no ha passat res. **`back` és la tornada i val `true` si no es diu**: quan l'app que s'ha obert s'acaba —o tres tecles la tanquen— la que va preguntar torna a la pantalla, que és el que vol un llançador i l'única manera de tornar en una placa de tres tecles. `app.run(id, false)` és un traspàs sense tornada. **El motiu del no és del sistema**: l'app que es demana a si mateixa, un `id` que no és al disc i una app d'una altra placa es diuen amb el seu `message` (`E_INVAL`), i no passa res més |
| `app.view()` | enter | **La vista d'una app, per què és el nombre de pàgines que té a sobre de la primera** (0.163.0): `0` és una app que només té una pàgina, i és el que el sistema llegeix quan un pessic demana tornar (vegeu `app.view_push`) |
| `app.view_push()` | enter | **Una pàgina a sobre de la que hi havia** (0.163.0): una app que obre un detall sobre la llista ho diu amb aquesta crida, i torna el nombre nou. És el número el que fa que un pessic doni el detall en lloc de tancar l'app |
| `app.view_pop()` | enter | **La pàgina de dalt, tornada** (0.163.0): la mateixa que baixa el pessic i la que baixa un botó de Tornar. Es queda a zero i no baixa de zero |
| `app.raise()` | — | Demana passar a primer pla (només té sentit dins d'una finestra d'alarma) |
| `app.save(t)` ⏳ | — | Desa una taula petita a `/data/<id>/state.json` |
| `app.restore()` ⏳ | taula o `nil` | Recupera el que va desar `app.save` |

Avui hi ha `app.id`, `app.args`, `app.info()`, `app.exit` i `app.run`; la resta encara no, i cridar-les dona
«attempt to call a nil value». On van: `app.save`/`app.restore` a **M4** (necessiten escriure al disc
i un escriptor de JSON), i `app.spawn`/`app.raise` quan una app els demani. Tot plegat és a
`docs/90`.

## `Promise`

| Crida | Torna | Notes |
|---|---|---|
| `p:and_then(ok[, err])` | promesa nova | Encadena: el retorn passa a la promesa nova (si és una promesa, la segueix) |
| `p:catch(err)` | promesa nova | Igual que `and_then(nil, err)` |
| `p:finally(fn)` | promesa nova | Corre passi el que passi i deixa passar el resultat original |
| `p:await()` | el valor, o llança | **Només dins d'una tasca**; si no, `E_NOTASK` |
| `p:timeout(ms)` | promesa nova | Es rebutja amb `E_TIMEOUT` passat el temps |
| `p.state` | `"pending"`, `"ok"`, `"err"` | Camp, no mètode |
| `Promise.all(llista)` ⏳ | promesa | Es resol amb una taula de resultats |
| `Promise.race(llista)` ⏳ | promesa | Es resol amb el primer que acabi |
| `Promise.resolve(v)` / `Promise.reject(e)` | promesa | Per fer codi uniforme |
| `Promise.is(v)` | booleà | |

**No es diu `p:then`**: `then` és paraula reservada de Lua i només es podria cridar com a
`p["then"](p, ...)`, que és una trampa per a qui escriu una app. El nom és `and_then`.

## `console`

| Crida | Torna | Notes |
|---|---|---|
| `console.write(s)` | enters escrits | Síncrona, amb memòria intermèdia |
| `console.print(...)` | — | `print` global fa el mateix |
| `console.flush()` ⏳ | — | Força la sortida |
| `console.read_line()` | promesa amb la cadena sense `\n` | Una lectura alhora: la segona dona `E_BUSY` |
| `console.read(n)` ⏳ | cadena (fins a `n` bytes) o `nil` | |
| `console.read_byte()` ⏳ | promesa amb l'enter 0–255 | **El proper byte del teclat**, sense línia i sense eco: el que necessita un editor (0.65.0) |
| `console.raw([on])` | booleà | **El teclat és del teclat i no de la línia** (0.65.0): sense eco, sense edició i sense CRLF traduït; es llegeix amb `console.read_byte`. Sense argument diu com està |
| `console.on_data(fn)` ⏳ | — | Mode push: `fn(tros)` per cada tros que arribi |
| `console.is_foreground()` | booleà | `false` dins d'una finestra d'alarma |

**Avui hi ha** `console.print`, `console.write`, `console.read_line`, `console.read_byte`, `console.raw` i
`console.is_foreground`.

### Mode cru: les tecles i no les línies (0.65.0)

Un prompt vol una línia; **un editor vol les tecles**. `console.raw(true)` canvia què és el teclat:

- **No hi ha eco i no hi ha edició**: el retrocés no esborra res i `Enter` no acaba res, perquè el que
  arriba són bytes i el que en fa l'app és cosa seva.
- **Les fletxes arriben senceres**: `ESC [ A` són tres bytes que l'app llegeix com vulgui, i el sistema no
  se'ls menja.
- **`console.read_byte()` és el camí**: torna una promesa amb l'enter del proper byte. Els bytes que arriben
  sense ningú esperant-los **es guarden en una cua de 256** i la lectura següent els troba de seguida, que és
  el que fa que escriure sigui més ràpid que el torn del nucli (docs/75).
- **La sortida d'emergència segueix sent del sistema**: tres ESC seguits tanquen l'app, però una fletxa no
  compta com un ESC. Un `:q!` de l'editor és la porta normal; els tres ESC són la de quan l'app no escolta.
- **`console.raw(false)` la desfà**, i el que hi havia a la cua es llença: mitja línia no és cap tecla i mitja
  tecla no és cap línia.

```lua
console.raw(true)
term.clear()
local b = console.read_byte():await()   -- 27 quan es prem ESC
if b ~= 27 then console.write(string.char(b)) end
console.raw(false)
```

Això és el que fa una app que vol el teclat sencer per a ella: `term` per dibuixar i
`console.read_byte` per llegir (docs/85).

### Mentre una app viu, el teclat és seu

No és una comoditat: és el contracte. Quan una app arrenca, la shell **deixa de mirar el que
s'escriu**; els bytes van a l'app, que els fa servir per a `console.read_line()` o per a res. La
shell torna a escoltar quan l'app tanca.

- **`console.read_line()`** torna de seguida una promesa i la resol quan es prem Enter, amb la línia
  sense el salt. L'eco i el retrocés els fa el sistema, com qualsevol terminal.
- **Una lectura alhora.** Una segona crida mentre la primera espera dona `E_BUSY`.
- **`nil` encara no arriba mai**: no hi ha senyal de fi d'entrada a la PAL d'aquesta versió, o sigui
  que la promesa es resol amb una línia o no es resol. Quan l'app tanca, l'estat sencer se'n va.
- **La sortida d'emergència és del sistema, no de l'app**: tres ESC dins 1,5 s tanquen l'app i tornen
  el prompt encara que no estigui llegint; i una línia que digui `!!exit`, quan l'app **no** està
  dins d'un `read_line`, fa el mateix. Si està llegint, `!!exit` és una línia com qualsevol altra, i
  l'app decideix què en fa.
- **I tres ESC també aturen una ordre que espera la ràdio** (31/10). No és una app, però és una cosa
  que dura: un `provision` que espera un telèfon no podia sortir amb la tecla que tothom prova, i
  només hi havia `wifi stop`. Ara ESC ESC ESC fa el mateix que `wifi stop`: deixa la feina i torna el
  prompt.
- Una tecla de cursor o de funció arriba com a `ESC [ …`: **no compta** com un ESC de la sortida
  d'emergència.
- El terminal envia `\r\n`, i les dues meitats poden arribar separades: **el LF que segueix un CR no
  és un segon Enter** ni una línia buida que respongui la lectura següent.

```lua
console.print("escriu alguna cosa")
local line = console.read_line():await()
console.print("has dit: " .. line)
```

## `term`

Codis ANSI per fer interfícies de text. Si `sys.caps().ansi` és `false`, les funcions no fan res i
no donen error (un terminal que no els entén no s'ha de trencar).

| Crida | Notes |
|---|---|
| `term.clear()` / `term.reset()` | Esborra la pantalla / torna a l'estat normal |
| `term.color(fg[, bg])` | Colors de la taula de constants |
| `term.move(row, col)` | Cursor (1,1 és a dalt a l'esquerra) |
| `term.hide_cursor()` / `term.show_cursor()` | |
| `term.width()` / `term.height()` | Amplada i alçada en caràcters, de `cfg/console.json`; per defecte 80×24 |

Constants: `term.BLACK`, `term.RED`, `term.GREEN`, `term.YELLOW`, `term.BLUE`, `term.MAGENTA`,
`term.CYAN`, `term.WHITE`, `term.BOLD`, `term.DIM`.

## `time`

| Crida | Torna | Notes |
|---|---|---|
| `time.now()` | enter | Segons des de l'època (UTC) |
| `time.now_ms()` | enter | Mil·lisegons des de l'època |
| `time.utc([t])` | taula `{year, month, day, hour, min, sec, wday, yday}` | El moment tal com el compta el xip, o l'`t` que se li dona |
| `time.date([t])` | taula | El mateix moment **en l'hora de la persona** (`cfg tz`) |
| `time.tz([offset])` | enter | El desfasament en minuts: es llegeix sol i s'escriu amb un argument |
| `time.leap([on])` | booleà | **Si el febrer té el vint-i-nou** (0.170.0): es llegeix sol i s'escriu amb `true`/`false` o les paraules `on`/`off`, que són les del prompt (`cfg leap on`) |
| `time.week([dia])` | enter | **Quin dia comença la setmana** (0.170.0): `1` dilluns —el valor del sistema— o `7` diumenge, en el mateix compte que `wday`, i les paraules `monday`/`sunday`. `ui.month` gira les seves set columnes amb això |
| `time.format(t, fmt)` | cadena | `%Y-%m-%d %H:%M:%S` — **encara no hi és** |
| `time.parse(s, fmt)` | enter o `nil, error` | **Encara no hi és** |
| `time.set(epoch)` | — | Permís `rtc_set` — **encara no hi és** |
| `time.sync()` ⏳ | promesa | NTP; requereix xarxa (avui hi ha `net.ntp`, que és el que fa la feina) |
| `time.caps()` | `{source, synced, wake}` | `source` és `"soc"`, `"rtc"` o `"none"`; `wake` és `"yes"` o `"no"` (si aquesta placa pot armar un despert per a una alarma) |

`time.now()` pot **saltar endavant** quan arriba una sincronització. Per mesurar durades, sempre
`sys.time_ms()`.

**Avui hi ha** `time.now()`, `time.now_ms()`, `time.utc`, `time.date`, `time.tz`, `time.leap`, `time.week` i `time.caps()`.
`format`, `parse`, `set`, `tz` amb noms de zona i `sync` encara no existeixen; l'hora la dona el rellotge i cada u la trenca
com vol (docs/60).

### `time.utc` i `time.date`: el moment en paraules (0.125.0)

Un número no és una data, i **el calendari viu una sola vegada** en aquest sistema (`hal/civil.h` passa d'època a data i
`kernel/calendar.h` diu quin dia de la setmana és). Aquestes dues crides són la porta perquè una app no hagi de tornar a
escriure la regla dels anys de traspàs:

```lua
local t = time.date()          -- l'hora de la persona
console.print(string.format('%04d-%02d-%02d %02d:%02d', t.year, t.month, t.day, t.hour, t.min))
console.print('wday ' .. t.wday .. ' yday ' .. t.yday)
```

- **`wday` és 1 per a dilluns i 7 per a diumenge**, el mateix compte que fa la resta del sistema (docs/60), i `yday`
  comença a 1 l'u de gener.
- **`min` i no `minute`**: és el nom del camp, i una taula de vuit camps es llegeix d'un cop d'ull.
- **`time.date` i no `time.local`**: `local` és una paraula de Lua i `time.local()` ni s'analitza —la mateixa paret que
  va fer que una alarma digui `in_ms` i no `in`—, i `date` és el nom que el projecte del paper donava a aquesta crida.
- **Sense desfasament, `time.date()` és `time.utc()`**, que és la veritat d'una placa a qui ningú ha dit on viu.

### `time.tz`: quin desfasament té l'hora de la persona (0.125.0)

El rellotge del xip compta **UTC** i una persona llegeix la seva hora: l'únic número que les uneix és el desfasament, i
és un ajust del sistema (`cfg tz` a la consola diu i escriu el mateix valor, docs/60).

```lua
time.tz()          -- els minuts que l'hora local va per davant d'UTC (0 per defecte)
time.tz('+01:00')  -- el mateix que `cfg tz +01:00`, i torna 60
time.tz(90)        -- noranta minuts, o sigui +01:30
```

- **Un text que no és un desfasament es refusa amb `E_INVAL`**, i el parser és el del nucli: la consola i una app
  accepten les mateixes paraules. **Un número sol és sempre minuts** (`60` fa una hora, `-90` una hora i mitja a l'oest)
  i la forma `±HH:MM` és la que porta els dos punts: `0130` no vol dir mai una hora i mitja.
- **No hi ha taula de zones**: això és un desfasament de dos bytes i no el mapa del món, i una placa on l'hora canvia
  dues vegades l'any s'ha de dir el número nou dues vegades l'any (docs/60).
- **Escriure-hi el desa a l'acte** (`cfg::set_tz_minutes` → `remember_tz`): no hi ha cap «desa-ho ara» ni cap manera de
  desfer-ho, o sigui que una app que vulgui desar la tria **en tancar** ha de tenir-la a la memòria i fer la crida a
  `on_close`, no en tocar el mapa. És el que demana l'app de la zona horària (docs/90).

### `time.leap` i `time.week`: el calendari que una persona vol (0.170.0)

Les dues decisions d'un calendari que **no** són una regla de la natura: si el febrer té el vint-i-nou i quin dia
comença la setmana. Són ajustos del sistema i no d'una app —`ui.month`, `time.utc`/`date` i qualsevol pàgina que
compti els dies d'un mes pregunten el mateix— i es desen a l'cfg com el desfasament, o sigui que sobreviuen a un
reinici (docs/60, docs/70).

```lua
time.leap()          -- cert: el febrer té vint-i-nou quan toca
time.leap(false)     -- el calendari sense el dia de traspàs; torna el valor nou
time.week()          -- 1, la setmana comença en dilluns
time.week('sunday')  -- 7, i torna 7
```

- **El dia de la setmana d'una data no es mou**: el 29 de febrer de 2024 cau en dijous amb el traspàs encès i amb
  ell apagat, perquè el cel no és una preferència. El que canvia és **quants dies té el febrer**, que és el que una
  pàgina compta.
- **`time.week` pren dilluns o diumenge i res més** (1 o 7, o les dues paraules): una setmana que comença en dimecres
  no és un calendari que una persona demani, i el que no es pot llegir del disc no s'hi escriu. Un número o una
  paraula que no siguin aquests donen `E_INVAL`.
- **Escriure-hi desa a l'acte**, com `time.tz`: no hi ha cap «desa-ho ara», o sigui que una app que vulgui desar la
  tria en tancar ha de fer la crida a `on_close`.
- **`ui.month` gira amb el primer dia**: els set noms de `weekdays` que l'app dona són **de dilluns a diumenge** i el
  sistema els gira amb l'ajust, o sigui que una app escriu una llista i el vidre ensenya la setmana que la persona ha
  triat (docs/30, docs/92).

## `alarm`

Les alarmes **no guarden funcions de Lua**. Guarden un identificador i un payload, i quan vencen el
sistema crida `on_alarm(id, payload, meta)` de l'app propietària. Això és el que permet que
sobrevisquin a un reinici i a la destrucció de la VM.

| Crida | Torna | Notes |
|---|---|---|
| `alarm.at(spec)` | enter (id) | `spec` = `{on=època}` o `{in_ms=ms}`, més `payload`, `repeat`, `raise`, `persist` |
| `alarm.after(ms[, payload])` | enter (id) | Drecera: alarma d'un sol cop |
| `alarm.cancel(id)` | booleà | |
| `alarm.list()` | taula | `{id, on, repeat, persist, raise}` de cada alarma |
| `alarm.caps()` | `{max, min_period_ms, wake}` | `max` alarmes simultànies, i `wake` diu si la placa pot despertar-se sola (`"yes"`/`"no"`) |

Camps de `spec`:

- `payload`: qualsevol valor que es pugui desar com a JSON (petit).
- `repeat`: `nil` (un cop), un número (segons) o `"daily"`.
- `raise`: si és `true`, quan venç l'app passa a primer pla.
- `persist`: si és `true`, sobreviu a un reinici. Per defecte `true` si `repeat` hi és.

`meta` que rep `on_alarm` és `{late_ms, missed}`: quant tard s'ha entregat i quantes repeticions
s'han perdut mentre el sistema era apagat.

**Avui hi ha** `alarm.at`, `alarm.after`, `alarm.cancel`, `alarm.list`, `alarm.caps`, `on_alarm` i
`console.is_foreground()`. Cinc coses que l'app ha de saber:

- **`in_ms` i no `in`**: `in` és una paraula reservada de Lua i una taula escrita `{in = 1000}` ni
  tan sols s'analitza (`unexpected symbol near 'in'`, mesurat el 31/10). La forma antiga
  `{["in"] = 1000}` encara funciona, però el nom bo és `in_ms`, com a `power.sleep`.
- **I `a.repeat` tampoc no es pot escriure**: el camp que torna `alarm.list()` s'anomena `repeat` i
  és una paraula reservada, o sigui que es llegeix `a["repeat"]`.
- **El payload és una cadena o un número.** Una taula dona `E_INVAL` fins que hi hagi un codificador
  JSON; dir-ho és millor que tornar una cosa diferent de la que es va desar.
- **`on_alarm` no pot esperar**: es crida entre dos torns de l'app, quan la coroutine de l'app està
  suspesa, i no des de dins. Una crida a `await` allà dona error.
- Dins d'una **finestra** (l'app no estava carregada), el que l'app escriu va al prompt del primer
  pla: encara no hi ha una consola separada per a la finestra.

**Si l'app arma alarmes al seu chunk principal, ho ha de fer només al davant.** Una finestra
executa l'entry per carregar l'app, i després crida `on_alarm`: sense el guard, cada finestra arma
una alarma nova i el sistema es crida a si mateix per sempre. Provat a la placa i a l'emulador
(30/10), i és el que fa l'app d'exemple `clock`:

```lua
function on_alarm(id, payload, meta) ... end
if not console.is_foreground() then return end   -- dins una finestra, res més
```

Les tres crides d'una alarma, una a una: `alarm.list()` és la **cua del sistema** —quantes n'hi caben,
quines hi ha i en quin ordre, i cancel·lar-ne una del mig—; `alarm.after(ms)` arma un temporitzador que
demostra que l'app suspesa no atura el nucli; i una alarma **persistent i amb `raise`** és la que fa que
el sistema apagui la placa i la torni a encendre amb el rellotge (docs/60). `app.exit()` a dins d'un
`on_alarm` és el cas complet: l'app arma l'alarma, marxa i torna per la finestra d'alarma quan l'hora
arriba.

**`persist` sí que hi és**: una alarma persistent es desa a la NVS amb l'**època a dins** (el
monòton torna a començar a cada arrencada), i en arrencar les vençudes es disparen amb el seu
`late_ms`. Sense època no s'escriu res, perquè una hora relativa no vol dir res després d'un
reinici (docs/60).

## `epub`

**Un llibre del disc, un capítol seu i una pàgina del capítol pintada en un canvas** (7/11, 0.132.0). És la
porta del lector: el motor —el zip, el tokenitzador i el paginador— és del sistema i l'app només demana pàgines.

| Crida | Què fa |
|---|---|
| `epub.open(path)` | obre el llibre d'un camí absolut (`/littlefs/epub/x.epub`); `true`, o `nil` i el motiu |
| `epub.entries()` | quantes entrades té el llibre |
| `epub.name(i)` | el nom de l'entrada `i` (comptant des d'u) |
| `epub.first_chapter()` | el nom del primer fitxer que sembla un capítol |
| `epub.chapter(name)` | **infla** aquest capítol i en diu quants bytes té |
| `epub.read(name)` | **una fitxa petita del llibre sencera** (`META-INF/container.xml`, l'OPF, l'índex, un full d'estil): els bytes, o `nil` i el motiu (0.142.0). El bloc és del llibre —es demana la primera vegada i fa 64 KB— i la porta en fa una còpia a una cadena de Lua |
| `epub.picture(canvas, name)` | **una foto del llibre al canvas, pel seu nom sencer dins del llibre** (`OEBPS/img/cover.png`) i **centrada**: `true`, o `nil` i el motiu (0.144.0). És la portada: el nom ja ve resolt de l'OPF i no d'un capítol, i **la foto conserva el seu paper** (no s'inverteix a la pàgina fosca; docs/46, C7 i C9) |
| `epub.thumbnail(name[, max])` | **el cau del llibre, escrit al costat del llibre** (0.146.0): la foto `name` escalada a `max` píxels pel costat més llarg (`64` si no es diu, i 256 com a sostre) i desada com un `ICS1` en un fitxer amagat amb el nom del llibre a dins, `.<fitxer del llibre>.cache.ics` (`/epub/.la-vaca-cega.epub.cache.ics`). Torna `true` quan el cau hi és —escrit ara, ja hi era o no hi ha espai per a ell— i `nil` i el motiu quan la foto no es pot llegir. **L'escriu el sistema perquè una app no escriu al costat del llibre** (docs/46, C11), **no el reescriu si ja hi és** i **pregunta l'espai abans d'escalar**; llegeix el cau qui vulgui, perquè és una foto d'aquest sistema |
| `epub.font(name)` | **la lletra que porta el llibre** (0.150.0, docs/46 D6): l'entrada `name` del zip —un TrueType, un OpenType o un **WOFF**, que el nucli obre taula per taula— convertida en una **cara**, que és el mateix objecte que torna `ui.load_font` (`font:release()`, i es dona a `epub.page` com `face` o `title_face`). **Les tres cares d'una app són tres per a les dues portes**: una cara de llibre pren un dels tres llocs. `nil` i el motiu quan el llibre no porta aquell fitxer, quan és un **WOFF2** (és Brotli, i aquest sistema no en té) o quan la cara no es pot llegir |
| `epub.page(canvas, from, opts)` | maqueta una pàgina des del byte `from` i **la pinta en aquell canvas**; torna `next, ended, used, has_picture` |
| `epub.close()` | tanca el llibre i torna la memòria |

`opts` és `{face, title_face, small_face, scale, title_scale, small_scale, indent, measure, dark}`, i cada camp té un
valor per defecte (la cara del sistema i els passos d'una pàgina de llibre). Les cares són **l'objecte que torna
`ui.load_font`**, no un número. `measure = true` maqueta i no pinta (per comptar o per anar enrere), i **`dark = true`**
(0.137.0) pinta **la mateixa pàgina amb la tinta i el paper canviats**: el mateix text als mateixos llocs, la tinta
blanca i el paper negre —que és el que l'app ha de deixar al canvas amb `cv:clear(0)`— i **les planxes invertides en
copiar-les** (docs/46, C9).

**La posició de lectura és un byte dins del capítol** i no un número de pàgina: `epub.page` torna `next`, que
és el byte on comença la pàgina següent. Un lector desa `(capítol, byte)`. Quan `ended` és cert el capítol
s'ha acabat.

Hi ha **un sol llibre obert** al sistema: el capítol és mig megabyte de memòria extra i el lector és un. Per
això el mòdul fa de llibre i no cal cap objecte amb metataula ni `__gc`: `open` tanca el que hi hagués.

El que el lector **no** fa: ni el CSS, ni columnes, ni partició de paraules, ni text vertical (docs/46, A2). Les
imatges **sí** que es descodifiquen des de la 0.133.0 i es pinten a la pàgina. **L'OPF no el llegeix el sistema**: el
llegeix l'app amb `epub.entries`, `epub.name` i `epub.read`, i **l'ordre dels capítols és el del `spine` del llibre**, no el dels noms dels fitxers (0.142.0, docs/46 C2).

## `qr`
| Crida | Torna | Notes |
|---|---|---|
| `qr.encode(text[, nivell[, mode]])` | taula o `nil, error` | `nivell`: `"l"`, `"m"` (per defecte), `"q"`, `"h"`; `mode`: `"auto"` (per defecte), `"numeric"`, `"alnum"`, `"byte"` |
| `qr.caps()` | taula | `{max_version=20, max_side=97, limit=4096, levels="lmqh"}` |

`qr.encode` torna `{side, version, mask, level, mode, modules}`:

- **`modules`** és una cadena de `side × side` caràcters `'0'` i `'1'`, fila a fila: el mòdul (0,0) és
  el primer caràcter. Una taula de taules costaria una reserva per mòdul, i això en un heap de 64 KB
  no s'aguanta (docs/75).
- **`mode`** és el que s'ha fet servir de debò: `"auto"` tria el més dens que hi càpiga (els números
  van a 10 bits per cada tres xifres, l'alfanumèric a 11 per cada dos caràcters, el byte a 8 per
  caràcter). Un mode forçat que no pot portar el text dona `E_INVAL` en comptes de canviar-se tot sol.
- **`level`** és el nivell de correcció real (quant de símbol es pot embrutar i seguir llegint). Amb
  més nivell, símbol més gran per al mateix text.
- **El límit d'una app és de 4 096 mòduls** (`limit`, un símbol de 64×64, versió 11): més que això no
  hi cap al sostre de la VM i es respon `E_TOOBIG`. El **sistema** no té aquest límit de 4 096: pot
  dibuixar fins a la versió 20 (97×97).
- El text ha de ser UTF-8/bytes; el mode **kanji no hi és** (necessita una conversió a Shift-JIS que
  aquest projecte encara no té) i no s'inventa: si mai cal, serà una crida més.

## `json`

| Crida | Torna | Notes |
|---|---|---|
| `json.get_string(text, clau)` | cadena o `nil` | Tradueix `\n`, `\t`, `\r`, `\"` i `\\` |
| `json.get_int(text, clau)` | enter o `nil` | Només nombres sense part decimal |

**No hi ha `json.decode`**: construir l'arbre d'un document costa RAM, i el sistema llegeix manifests
de quatre claus sense arbre (docs/75). Si una app necessita un arbre, es parla i es mira el número
abans, com tot el que creix.

## `net`

| Crida | Torna | Notes |
|---|---|---|
| `net.caps()` | taula | `{wifi, http, tls, prov}`: un bit només és cert si la placa ho té de debò |
| `net.connect(ssid[, pass[, ms]])` | **promesa** | Es resol amb `true` quan la xarxa és a dalt; 20 000 ms per defecte |
| `net.provision([pop[, servei[, ms]]])` | **promesa** | Obre l'AP de provisioning i es resol quan la placa **és a la xarxa**; 180 000 ms per defecte |
| `net.provisioned()` | booleà | La placa ja té una xarxa desada |
| `net.ntp([servidor[, ms]])` | **promesa** | Posa el rellotge amb un servidor de temps (`pool.ntp.org` per defecte) i es resol `true` quan el rellotge ja hi és. **No** deixa la ràdio encesa: la resposta és una hora, no una connexió. **Des de la 0.174.0 la promesa es resol de debò en una app**: fins llavors el pont no la desava on la xarxa la busca i un `net.ntp():await()` esperava per sempre, mentre la comanda `ntp` de la consola contestava (trobat pel cas de l'app `tz`, docs/85) |
| `net.add(ssid[, pass[, ms]])` | **promesa** | Desa la xarxa a la placa **i s'hi connecta**, sense cap app al mig |
**Tot passa per una cua.** `net.connect`, `net.scan`, `net.add`, `net.provision`, `net.forget`,
`net.ntp` i `net.connect_stored` no fan res de seguida: entren en una **cua de peticions** del nucli,
que en fa **una alhora**. Si la ràdio és lliure, la petició comença a l'acte (per això un `net.status()`
just després de demanar ja diu `connecting`); si no, es refusa amb `E_BUSY`, perquè una app té **un
sol lloc de promesa** i encuar-ne una segona seria deixar una promesa que no es resoldria mai.

**La ràdio no es queda encesa mai per a ningú.** Quan la cua es buida, es desconnecta i s'apaga. Una
tasca que necessita la xarxa —`net.ntp` i `http.get`— la **demana ella mateixa**: primer es
connecta amb el que la placa té desat i després fa la feina, tot dins la mateixa petició. No hi ha cap
estat «estic connectat» que duri: estar en una xarxa és una cosa que una tasca fa durant una estona
(docs/75, docs/90).

| `net.scan([ms])` | **promesa** | Es resol amb **quants** punts d'accés es poden llegir després amb `scan_record`. Sempre és un número: un escaneig que no troba res contesta `0` (mai `true`, que faria petar un `for`) |
| `net.scan_record(i)` | taula o `nil` | `{ssid, rssi}` de la xarxa `i` (1 a N) de l'últim escaneig |
| `net.stored()` | cadena o `nil` | El **nom** de la xarxa que la placa té desada |
| `net.connect_stored([ms])` | **promesa** | Connecta amb la xarxa desada, sense saber-ne la contrasenya |
| `net.forget()` | `true` o `nil, error` | Esborra la xarxa desada (decisió de la persona, no efecte secundari) |
| `net.disconnect()` | `true` o `nil, error` | Deixa anar la ràdio |
| `net.status()` | cadena | `"off"`, `"connecting"`, `"up"` o `"failed"` |

- **`net.connect` no bloqueja mai**: comença l'associació i torna de seguida. El nucli mira com va
  a cada torn, i el **termini el compta el sistema**, no la ràdio: si arriba abans que la xarxa, es
  deixa l'intent i la promesa falla amb `E_TIMEOUT` (i la ràdio es desconnecta).
- **Un intent alhora**: si n'hi ha un de corrent, `net.connect` dona una promesa que ja porta
  `E_BUSY`, sense tocar el primer.
- **Els errors arriben com a llançament**, com tota promesa (`await` llança, docs/30 §promeses): es
  recullen amb `pcall`. Codis: `E_UNSUPPORTED` (la placa no té ràdio), `E_BUSY`, `E_NONET` (la xarxa
  ha refusat) i `E_TIMEOUT`.
- **Si l'app mor mentre connecta**, l'intent es deixa anar: ningú no el vol i la ràdio no ha de
  quedar encesa per res (docs/75).

### Provisioning: la xarxa la porta el telèfon

La contrasenya **no s'escriu enlloc del repositori**. La placa obre el seu propi punt d'accés i
**l'app d'Espressif** (ESP SoftAP Provisioning, Android i iOS) li passa la xarxa:

1. La comanda `provision` (o una app amb `net.provision('lunokos', 'lunokos-prov')`) obre el
   provisioning **per Bluetooth**, que és el que el fabricant recomana (docs/50).
2. Al telèfon: obrir l'app, triar el dispositiu `PROV_xxxxxx`, escriure el **proof of possession**
   (`lunokos`) i la xarxa i la contrasenya.
3. La promesa es resol **quan la placa és a la xarxa**, no quan rep la contrasenya: si l'associació
   falla, l'app rep `E_NONET` i les credencials queden desades per tornar-ho a provar.

- **On van les credencials**: al magatzem de la placa (el mateix que fa servir el nucli Wi-Fi
  d'Espressif, a la NVS). El sistema no les torna a escriure mai més enlloc, i `net.forget()` les
  esborra.
- **El POP és el que impedeix que algú altre provisioni la placa** mentre l'AP és obert. L'ATOM no té
  pantalla per ensenyar-ne un de diferent cada cop, així que és una constant de l'app: **canvia-la**
  si et preocupa aquest moment.
- **Un board que ja té xarxa no torna a obrir l'AP**: `net.provision()` hi connecta directament amb
  el que té desat.
- **Després d'un reinici no es connecta sol** (encara): l'app que vol xarxa crida
  `net.connect_stored()`. Encendre la ràdio a l'arrencada és una decisió d'energia i es pren a M6
  amb el número a la mà.
- **`net.scan()` es resol amb un número**, no amb una llista: per saber si la ràdio veu res n'hi ha
  prou, i la llista no cap en un heap de 64 KB. És la primera pregunta quan una app del telèfon
  ensenya una llista buida.
- **`net.add(ssid, pass)` és el camí que no depèn del telèfon**: desa la xarxa a la placa i s'hi
  connecta en una sola promesa. Serveix quan l'app d'Espressif no pot llistar les xarxes (la llista
  la demana **a la placa**, i si l'escaneig falla l'app ensenya una llista buida), i serveix per a una
  placa sense ningú al davant. La comanda `wifi add` ho fa amb el nom i la contrasenya a la
  port sèrie — amb l'avís que **la contrasenya es veu mentre s'escriu**, que és el preu de no tenir
  pantalla.

## `http`

Una pàgina, i prou: una crida, una promesa, i la resposta és el codi d'estat i el cos.

| Crida | Torna | Notes |
|---|---|---|
| `http.get(url[, max_bytes[, ms]])` | **promesa** | Es resol amb `{status, body, bytes}`; **16 384 B** de cos i **20 000 ms** de termini per defecte |
| `http.download(url, path[, ms])` | **promesa** | Es resol amb `{status, path, bytes}`; **la pàgina sencera va al fitxer** i no passa pel heap, per llarga que sigui |

- **Va per la cua com la resta.** No cal `net.connect` abans: la petició es porta la xarxa ella
  mateixa (amb el que la placa té desat) i la deixa anar en acabar (docs/75).
- **`status` és el codi HTTP i no és un error**: un `404` és una resposta i arriba amb el seu cos.
  Els errors són els de la llista de baix.
- **El cos és una cadena de bytes** (`body`, i `bytes` en diu la llargada): pot no ser text i pot
  tenir zeros a dins.
- **`max_bytes` és un sostre i no un consell**: si la pàgina és més llarga, la promesa falla amb
  `E_TOOBIG` i **no arriba res tallat**. Mitja pàgina llegida com una pàgina sencera és pitjor que
  un error.
- **L'adreça té un màxim de 192 B** (el que porta la cua): una de més llarga es refusa amb
  `E_TOOBIG` en comptes de tallar-se, perquè mitja adreça demana una altra pàgina.
- **Una pàgina que no cap enlloc no es perd: va a un fitxer.** `http.download` la deixa sencera al
  camí que se li dona —`{status, path, bytes}`, i el fitxer és de qui llegeix el disc, amb
  `fs.open`—, i al prompt `http <url>` fa el mateix tot sol quan la pàgina passa dels 16 384 B: la
  desa a la targeta (o a la flash si no n'hi ha) i en diu el camí. **Mitja pàgina no arriba mai**: un
  fitxer que no és la pàgina sencera s'esborra i la promesa falla (docs/70).
- **El lloc on va la pàgina és el d'un fitxer**, i no un de qualsevol: una app escriu on escriuria
  amb `fs.write` —el seu `/data/<id>/`—, i el prompt, a qualsevol disc muntat escrivible (docs/70).
  Un camí que no és d'un disc d'aquesta placa refusa la petició amb `E_UNAVAIL` abans de demanar res
  al servidor.
- **El TLS no es pot desactivar i cap certificat viu al repositori**: la placa fa servir el paquet
  d'autoritats que porta el build d'Arduino, i un servidor que no hi és sota rebota amb `E_TLS`
  (docs/50).
- **Codis**: `E_NONET` (la placa no té cap xarxa desada on connectar-s'hi), `E_TIMEOUT`, `E_TOOBIG`,
  `E_TLS`, `E_HTTP`, `E_NOSPC`, `E_IO` (el disc no ha pogut escriure la pàgina), `E_UNAVAIL` (el
  lloc on havia d'anar no hi és) i `E_BUSY`.
- **La ràdio no queda encesa**, i la pàgina no es queda enlloc: el cos viu dins la cadena de Lua i,
  quan l'app el deixa anar, el recull el recolector; el d'un `http.download` viu al disc i és de qui
  l'obri.

```lua
local ok, page = pcall(function() return http.get('https://example.com/'):await() end)
if not ok then
  console.print('no page: ' .. tostring(page and page.code))
  return
end
console.print(page.status .. ' (' .. page.bytes .. ' bytes)')
console.print(page.body)
```

Una pàgina que no cap al heap es baixa a un fitxer i es llegeix per trossos:

```lua
local r = http.download('https://example.com/gran.html', '/data/jo/gran.html'):await()
console.print(r.status .. ' (' .. r.bytes .. ' bytes a ' .. r.path .. ')')
local f = fs.open(r.path, 'r')
console.print(f:read(200))
f:close()
```

El que es veu en una app és el que torna la promesa: `{status, body, bytes}`, amb el cos sencer a la
memòria quan hi cap.

## `fs`

| Crida | Torna | Notes |
|---|---|---|
| `fs.mounts()` | taula | `{path, type, writable}` de cada muntatge, sense l'espai (vegeu `fs.space`). L'ordre és el de la taula del nucli i n'hi caben **deu** |
| `fs.space([path])` | taula o `nil, error` | `{total, free}` del muntatge on viu el camí; `E_UNSUPPORTED` si el backend no ho sap |
| `fs.list(path)` | taula | `{name, type, size, mtime}` de cada entrada |
| `fs.stat(path)` | taula o `nil, error` | `{type, size, mtime}` |
| `fs.mkdir(path)` / `fs.remove(path)` / `fs.rename(a, b)` | — | El `E_PERM` de la taula d'errors és **reservat**: el model de permisos no existeix encara, i avui escriure el decideix el terreny de l'app, no el manifest |
| `fs.format(path)` | `{erased}` o `nil, error` | **Buida el volum on viu el camí** (0.171.0, i la flash des de la 0.171.1): cada fitxer i cada carpeta de la seva arrel, i `erased` diu quants n'han anat. A `/nvs` esborra **totes les claus** del magatzem; a `/sd` deixa la targeta buida i **amb el seu FAT**, i a `/littlefs` buida la flash del sistema i li deixa el seu superblock, perquè el que s'esborra és el contingut i no el sistema de fitxers. El camí ha de ser un volum i no una carpeta (docs/70) |
| `fs.open(path[, mode])` | objecte fitxer | `mode`: `"r"`, `"w"`, `"a"`, `"r+"` |
| `fs.read(path[, max])` | cadena o `nil, error` | El fitxer sencer, o com a molt `max` bytes |
| `fs.read_at(path, offset, len)` | cadena o `nil, error` | Accés aleatori sense obrir |
| `fs.write(path, data)` | enters | Escriu sencers (temporal + reanomenar) |
| `fs.data([sub])` | cadena | `/data/<id>` (sense barra final, que és el que diu el codi) o `/data/<id>/<sub>` amb un nom a dins |
| `fs.cfg([unitat])` | taula o `nil, error` | `{unit, dir}`: on van els ajustos d'aquesta app. Amb `"auto"`, `"sd"` o `"littlefs"` els mou (docs/70) |

**Avui hi ha** `fs.open`, `fs.list`, `fs.stat`, `fs.read`, `fs.read_at`, `fs.write`, `fs.mkdir`,
`fs.remove`, `fs.rename`, `fs.format`, `fs.data`, `fs.mounts`, `fs.space` i `fs.cfg`.

De l'**objecte fitxer** (`f = fs.open(path[, "r"|"w"|"a"|"r+"])`):

- `f:read(n)` i `f:read("*l")` tornen **`nil`** al final del fitxer (com a Lua); `f:read("*a")` torna la resta i, si no en queda, la cadena buida.
- `f:write(s)` torna els bytes escrits, i **no és atòmica**: per deixar un fitxer sencer o res, `fs.write`.
- `f:seek(["set"|"cur"|"end"[, off]])` torna la posició nova; `f:size()`; `f:close()`.
- `f:flush()` no fa res: cada escriptura ja va al backend, i el fitxer és sencer quan es tanca.
- **Hi ha vuit fitxers oberts com a màxim**, i el novè dona `E_NO_HANDLES`. Les lectures i escriptures
  senceres (`fs.read`, `fs.write`) també obren i tanquen pel mateix camí, així que amb vuit fitxers
  oberts no hi cap res més fins que se'n tanqui un.

El que una app ha de saber:

- **Cap fitxer és massa gran per llegir-lo**: el sistema el llegeix en **finestres de 512 B** que van
  omplint la cadena de Lua, així que ni el fitxer sencer cap en un buffer de C ni l'app ha de saber
  res de finestres. L'únic límit és la **memòria de l'intèrpret**; per això un fitxer gran es llegeix a
  trossos amb `fs.open` i `file:read(n)`.
- `fs.read(path[, max])`: amb `max`, **com a molt** aquests bytes (un prefix d'un fitxer més gran);
  sense `max`, el fitxer sencer. `file:read(n)` torna fins a `n` bytes, `file:read("*a")` fins al final
  i `file:read("*l")` una línia **sencera**, sigui com sigui de llarga.
- `fs.write` és **atòmica**: escriu un temporal al costat i el reanomena. Si falla a mitges, el
  fitxer vell segueix sencer. Torna els bytes escrits.
- **Una app només escriu dins el seu terreny**, `/data/<id>/` (`fs.data([sub])` en dona el camí),
  `/cfg/` (que és el seu `cfg/<id>/`) i **`/rtc`**, i no pot llegir el d'una altra: això dona
  `E_INVAL`, no `E_NOENT`. Fora del seu terreny només pot **llegir** `/res/` —el seu, que és
  `apps/<id>/res/` al costat de l'app—, `/apps/` i `/sd/`; les unitats del sistema (`/littlefs/`,
  `/nvs/`) no són seves i també responen `E_INVAL` (docs/70).
- **`fs.format` és l'única porta que arriba fora del terreny de l'app** (0.171.0). No és un forat del
  model: és el que vol dir buidar un volum, i el que la tanca és la targeta d'avís que ha de respondre
  una persona —la pàgina de `wipe` no fa res sense un accept— (docs/70, docs/90). **El camí ha de ser un
  volum** i no una carpeta: `/nvs`, `/sd` i `/littlefs` ho són i `/sd/notes` respon `E_INVAL`.
  **I una carpeta d'un volum tampoc no ho és** (0.171.1): `/data`, `/apps`, `/res` i `/fonts` són files de
  la mateixa flash, o sigui que buidar-ne una buidaria el disc sencer sota el nom d'una carpeta, i el
  nucli ho refusa amb `E_INVAL` (docs/70).
- **`/littlefs` és un volum com la targeta** (0.171.1): es buida fitxer a fitxer i carpeta a carpeta i
  **conserva el seu superblock**, perquè el que s'esborra és el contingut i no el sistema de fitxers.
  És la porta que deixa la placa amb el prompt i res més —les apps del sistema, les seves imatges i les
  cares viuen allà— i per això la targeta d'avís de `wipe` la descriu així (docs/70).
- **`/rtc` i `/fram` són l'excepció i són memòria crua**: 7 168 B que sobreviuen a un son profund i
  256 B que no necessiten el corrent, sense mapa ni claus, i **el format el decideix l'app que hi
  escriu** (`/rtc` a seques, que és el fitxer sencer). S'hi escriu amb un mànec —`fs.open('/rtc',
  'w')` i `file:write(…)`— o amb `fs.write` des de la 0.65.1, que hi va **al lloc** perquè una
  escriptura atòmica necessitaria un reanomenament que una memòria no té: els bytes al començament i la
  resta de la memòria a zero, i `E_NOSPC` si no hi cap sencera. `fs.space` diu la mida de cadascuna i
  tota lliure, perquè no s'hi assigna res (docs/50, docs/70).
- **No hi ha quota**: el que doni el disc. Quan s'ha acabat, l'escriptura respon `E_NOSPC` i el fitxer
  vell queda sencer, perquè `fs.write` és atòmica.
- `fs.stat` torna `{type = "file"|"dir", size, mtime}`; `mtime` és 0 perquè cap backend el dona.
- `fs.list` torna una taula de `{name, type, size, mtime}` en l'ordre que dona el directori.
- **`/cfg/` és el lloc dels ajustos de l'app i no cal saber on és**: `fs.write('/cfg/s.json', s)`
  acaba a `cfg/<id>/s.json` de la unitat que el sistema hagi triat —la targeta si n'hi ha, la flash
  si no—, i `fs.cfg()` ho diu (`{unit = "sd"|"littlefs", dir = "/sd/cfg/<id>"}`) quan a l'app li
  interessa ensenyar-ho. `fs.cfg("sd")` ho mou; una unitat que no hi és respon `nil, E_UNAVAIL`, i
  els fitxers que ja hi havia **no es mouen** (docs/70).
- **Per veure-ho de mà**: `mounts` al prompt llista els muntatges i l'espai de cadascun.
- **`fs.mounts()` no pregunta l'espai**: la llista surt del que el nucli té muntat, i prou. Preguntar
  l'espai vol dir passejar pel sistema de fitxers, i una app que només vol la llista no ho ha de
  pagar.
- **`fs.space(path)` és la que ho pregunta**, i diu la veritat: `nil, {code = "E_UNSUPPORTED"}` quan
  el backend no ho sap. A l'emulador dona **els números de la placa** —la partició de la flash, la
  targeta de 16 GB i, al `/rtc`, la seva mida, tota lliure—, i a la placa els de debò; el `/nvs` no
  els dona a cap de les dues bandes, perquè guarda entrades i no bytes (docs/70, docs/85).
- **Els camins amb espais van entre cometes** a la shell, no a Lua: `cat "res/el meu fitxer.txt"`.
  Des de Lua el camí és una cadena i els espais són espais.

Objecte fitxer: `f:read(n)`, `f:read("*l")`, `f:read("*a")`, `f:write(s)`,
`f:seek([whence[, off]])`, `f:flush()`, `f:size()`, `f:close()`. Si no es tanca, el recol·lector el
tanca, però el comptador de fitxers oberts és de 8: tancar és cosa de l'app.

## `store` ⏳

**Avui no hi és.** És el contracte d'una cosa que encara no s'ha demanat: clau-valor petit per app,
sobre `/data/<id>/store.json`, amb claus de 15 caràcters com a màxim.

| Crida | Torna |
|---|---|
| `store.get(k)` | valor o `nil` |
| `store.set(k, v)` | — |
| `store.remove(k)` | — |
| `store.all()` | taula |
| `store.clear()` | — |


## `led`

El llum de la placa: **un** LED al ATOM Lite i **vint-i-cinc** al Matrix (5 per 5, un panell). El
contracte és el mateix als dos —`led.set` pinta **tot el llum** d'un color i `led.pixel` en pinta
**un**— i el que canvia és quants n'hi ha, que ho diu `led.caps().count` (docs/50).

**Dues capes i prou.** El **sistema** diu què fa la placa —arrenca, la ràdio treballa, salta una
alarma, alguna cosa ha fallat—, i **l'app** hi pot posar el que vulgui mentre viu: el que demana
l'app guanya mentre dura, i quan se'n va el llum torna a ser del sistema (docs/50).

| Crida | Torna | Notes |
|---|---|---|
| `led.caps()` | `{count, max, taken}` | Quants llums té la placa, **fins a quin valor** es pot encendre un canal, i si algú els ha pres (una app amb un color o amb un efecte) |
| `led.set(r, g, b)` | `true` o `nil, error` | El color que vol l'app **mentre és al davant**, a tots els llums. **`E_BUSY`** si el llum està ocupat (vegeu «La interrupció») o si l'app té un efecte corrent |
| `led.pixel(i, r, g, b)` | `true` o `nil, error` | **Un** llum: `i` compta des de zero, i un índex que la placa no té es refusa amb `E_INVAL`; `E_BUSY` si el llum està ocupat |
| `led.off()` | `true` o `nil, error` | El mateix que `led.set(0, 0, 0)`, i es llegeix millor |
| `led.play(t)` | `true` o `nil, error` | Un **efecte** que corre sol mentre l'app fa la seva feina (vegeu més avall) |
| `led.stop([ms])` | `true` o `nil, error` | Deixa anar el llum: **ara** (sense número) o amb un **fade a negre** de `ms` mil·lisegons |
| `led.effect()` | taula o `nil` | L'efecte que l'app està jugant, amb `kind`, `color`, `period`, `cycles`, `pixel`, `level` i **`elapsed`** (quants ms fa que corre). `nil` si no n'hi ha cap |
| `led.system([nom[, t\|"default"]])` | taula o `true` o `nil, error` | Els quatre efectes del sistema: sense arguments els quatre, amb un nom aquell, amb una taula el canvia, amb `"default"` hi torna a posar el del fabricant |

### `led.play`

```lua
led.play{kind = 'breathe', color = {0, 0, 200}, period = 3000, cycles = 2, level = 120}
led.play{kind = 'sos', color = {200, 0, 0}}                       -- un avís de socors
led.play{kind = 'blink', period = 500, cycles = 0}                -- fins que es pari
led.play{kind = 'breathe', pixel = 12, period = 2000, cycles = 0} -- un llum del panell
```

| Camp | Per defecte | Què és |
|---|---|---|
| `kind` | **obligatori** | `breathe` (la intensitat puja i baixa com una respiració), `blink` (encesa i apagada), `sos` (el codi Morse del socors), `bounce` (una pilota que bota cada cop més de pressa) o `none`/`off` (cap) |
| `color` | blanc | `{r, g, b}`, cada canal de 0 a 255 i retallat pel sostre de la placa |
| `color2` | el mateix que `color` | L'altre extrem de la **rampa**: l'efecte va del primer color al segon al llarg de cada cicle, que és el que fa que un respiració derivi i un rebot canviï de to a mesura que es fa més ràpid |
| `period` | **5 000** breathe, **800** blink, **200** sos, **700** bounce | Un **cicle sencer** en mil·lisegons; al `sos` és **un punt** i prou, i una volta del missatge són 34 punts amb els seus silencis |
| `cycles` | `0` | Quants cicles abans de deixar anar el llum. **`0` vol dir per sempre**: fins que es pari o fins que l'app marxi |
| `pixel` | `-1` | `-1` és **tot el llum**; un número és **un** llum del panell, comptat des de zero, i la resta queden apagats. Un número que la placa no té es refusa amb `E_INVAL` |
| `level` | **20** | El més brillant que pot arribar aquest efecte, sobre 255. Vint de sèrie perquè un llum que diu una cosa no ha de ser el més brillant de la taula; una app que en vulgui més ho demana, i el sostre de la placa encara mana per sobre |

- **Un efecte no atura l'app ni el nucli.** No hi ha cap fil nou ni cap espera: el nucli pregunta
  a final de cada torn quin color toca **en aquell mil·lisegon**, i el pinta si ha canviat. Si un
  torn arriba tard, es pinta el frame d'aquell moment i no una cua de frames pendents, o sigui que
  el llum no pot anar ni endarrerit ni estirat (docs/10, docs/75). El preu és el d'un app que no
  cedeix: la llum es queda quieta igual que tot el sistema (docs/20).
- **Un període per sota de 120 ms s'arrodoneix a 120.** Un llum que parpelleja cada mil·lisegon no
  és un missatge, és una molèstia, i el terra és del nucli i no de l'app (docs/50).
- **`led.set` atura l'efecte**: l'última cosa que demana l'app és el que fa el llum. I prendre'l
  també acaba un moment del sistema que estava passant, perquè un moment és una nota i no un estat
  (vegeu més avall).
- **`led.stop(ms)` fa el fade des del que s'està veient** i després dona el llum al sistema, que aquí
  vol dir apagar-lo si no hi ha res més a dir.

### La interrupció: qui mana quan

El **sistema interromp l'app** quan ha de dir una cosa seva (una alarma, un error). I allò que l'app
havia posat **no es perd**:

1. **Abans de dir res**, el sistema desa el que l'app tenia —el color, el frame pintat llum a llum o
   l'efecte, amb el punt del cicle on era— i neteja la capa de l'app.
2. **Durant el moment, el llum és del sistema**, i l'app no el pot tocar: `led.set`, `led.pixel`,
   `led.off` i `led.play` responen **`E_BUSY`**. Per tocar el llum a mà, res no l'ha d'estar animant.
3. **Quan el moment s'acaba**, el llum de l'app **torna exactament com era** (l'efecte continua on
   era, no al final de la interrupció) i el nucli crida **`on_light()`** de l'app, perquè durant el
   moment l'app pot haver canviat d'idea i el llum no l'ha escoltat (docs/40).

```lua
function on_light()
  -- El sistema ha acabat de dir la seva i el llum torna a ser meu.
  led.set(0, 0, 40)
end
```

**Qui guanya, en ordre**: un error tapa una alarma, una alarma tapa la ràdio i qualsevol cosa tapa
la respiració d'arrencada. La **ràdio** i la respiració d'arrencada **no són interrupcions**: són el
fons, i un color o un efecte de l'app els substitueix sense esperar ni haver de tornar-hi.
Un **error** que salta mentre hi ha un moment més fluix el substitueix (i no el desa: el moment no és
de l'app).

**I una persona mana sobre tots dos**: `led 00ff00` a la consola para el moment, no desa res i posa el
color que demana la persona (docs/30).

### Els quatre moments del sistema

| Moment | Què fa de sèrie | Quan |
|---|---|---|
| `boot` | respiració **blanca** que deriva a blau pàl·lid, 4 cicles de 5 s, nivell **20** | quan la placa arrenca, i **s'acaba**: un llum que respira tota l'estona en una taula és energia gastada per ningú. Un color o un efecte de l'app el substitueix sense avisar i sense que torni |
| `radio` | parpelleig **blau** que deriva a cian, 800 ms, nivell 20 | mentre la ràdio treballa (Wi-Fi, Bluetooth o ESP-NOW) |
| `alarm` | rebot **turquesa** que deriva a verd | quan venç una alarma del rellotge, sigui de qui sigui; **interromp** |
| `error` | SOS **vermell** que deriva a taronja | quan el nucli escriu un error al registre (`E`), que és el que veu una persona quan la consola és tancada; **interromp** |

L'ordre mana: un **error** no el tapa una alarma, i una alarma no la tapa la ràdio. I els quatre
són **de l'app**: `led.system()` els llegeix, `led.system('alarm', {...})` els canvia i
`led.system('alarm', 'default')` hi torna a posar els del fabricant. La llum d'una màquina no és un
gust que el nucli es quedi.

- **El llum és del sistema i l'app el pren mentre viu.** Quan l'app se'n va, el que havia pres
  **es deixa anar i no es torna al color d'abans**: un llum que sobreviu a la seva app és energia
  gastada per ningú. Passa igual si l'app acaba sola, si la tanquen amb tres tocs o si mor.
- **Al Matrix la imatge és de l'app i són vint-i-cinc llums.** Un `led.pixel` li dona el llum
  sencer, i és una imatge i no un color: vint-i-cinc crides fan un frame. `led.set` la torna a
  pintar tota d'un color si l'app canvia d'idea, i el que no passa mai és que la imatge quedi
  encesa quan l'app marxa — en marxar s'apaguen **tots** els llums (docs/50, docs/75).
- **En una placa sense llum aquest pas no costa res**: `led.*` respon `E_UNSUPPORTED` i deixar anar
  el color no fa res, perquè no hi ha res per apagar (docs/30).
- **El sostre d'un canal no és sempre 255.** `led.caps().max` diu quin és el de debò: **255** al
  Lite i **20** al Matrix, que és el número que dona el fabricant perquè vint-i-cinc LEDs de 2020
  en una caixa tancada s'escalfen i es poden fer malbé. **El Lite no té cap sostre reduït pel
  fabricant** —un sol WS2812 a 255 és el que diu el seu full de dades—, però el **nivell per defecte
  de tot efecte és 20** igualment, perquè un llum que diu una cosa no ha de ser el més brillant de la
  taula. El sostre el fa complir **el sistema**, no el driver: una app que demani 255 al panell en
  pinta 20, i la shell ho diu quan s'hi passa (`led: 00ff00 (this board stops at 20)`). Una app que
  vol una rampa que arribi dalt de tot l'ha de comptar amb `max`, com fan `light` i `matrix`
  (docs/50).
- **No cal fer res perquè es vegi**: el nucli el pinta a final de torn i només quan canvia. Un frame és
  una escriptura al fil i el llum també costa (docs/75).
- **Els nivells són fluixos a posta** (`level` per defecte de cada moment és el de la taula de dalt, i
  el d'una app és 255 si no diu res): un llum que diu una cosa no ha de ser el més brillant de la
  taula.
- Amb `sys.caps().led` una app sap si tot això existeix abans de demanar-ho, i l'exemple és l'app
  quin efecte juga i quant dura.


## `env`

L'aire: **temperatura i humitat**, del SHT30 de la Paper, a la I2C interna que comparteix amb el
rellotge i la FRAM (docs/50). Una sola crida perquè la mateixa lectura dona les dues coses.

| Crida | Torna | Notes |
|---|---|---|
| `env.read()` | `{celsius, humidity}` o `nil, error` | Temperatura en **°C** i humitat **relativa en %** (0..100) |

- **`E_UNSUPPORTED` en una placa sense sensor** (els ATOM) i **`E_IO` si el xip hi és i no contesta**.
  Són dues coses diferents i una app les ha de poder distingir: la primera no canviarà mai, la segona
  pot ser un cable o un bus.
- **No torna zeros mai**: un zero és una temperatura i una humitat de debò, i inventar-lo faria que
  una app no pogués saber si el que llegeix és cert.
- **No és una promesa, i costa uns sis mil·lisegons.** El xip mesura quan se li demana i, mentre
  mesura, té la línia de rellotge agafada: el torn del nucli que fa la lectura dura això i prou. Més
  que una promesa per a un número que es llegeix un cop cada minut, i dit aquí perquè no sorprengui
  (docs/50). **Aquella espera l'ha de deixar passar el bus**: el driver de la I2C dóna tretze
  mil·lisegons de marge (0.10.1) i, amb el que porta de fàbrica —cent microsegons—, el xip es donava
  per mort i `env.read()` responia `E_IO` amb la placa sencera dient que no hi havia sensor (docs/50).
- **Dos CRCs** se supervisen abans de donar el número: una lectura amb el CRC creuat és una lectura
  que no ha passat i torna `E_IO`, perquè cap número és millor que un número fals.
- Una app que la llegeixi cada dos segons en té prou amb `env.read()` dins del seu bucle.

## `ui`

La vista de l'app: **el seu propi canvas**, i el moment en què demana al sistema de posar-lo al vidre.
El vidre té un sol buffer i és del sistema; l'app dibuixa al seu, i `ui.present` és l'única cosa que el
fa arribar a la pantalla. **El sistema no dibuixa mai la pàgina d'una app** —ni splash, ni netejar quan
una app marxa—: cada app decideix **què** es dibuixa, amb **quina qualitat** i **quan** (docs/50).

L'únic que el sistema pinta són **les 48 files de dalt**: la barra amb l'hora, el títol de l'app i la
càrrega, que no és de cap app (docs/50, «La barra del sistema»). Per això la vista d'una app comença a
la fila 48 i `ui.caps().y` ho diu.

**I les 48 files poden ser de l'app** (0.136.0, docs/46 D1): `ui.full_screen(true)` demana al sistema de
quedar-se tota la pantalla, i el sistema li dona —la vista passa a **540 × 960** i comença a la fila 0, i
la barra s'amaga i no es torna a pintar fins que la pàgina deixa de ser sencera—. El sistema li diu la
mida nova amb `on_resize`, que és el mateix ganxo d'un gir. **La sortida no és de l'app**: tres tocs
seguits sobre el vidre són del sistema, que se'ls queda de la cua abans que l'app els llegeixi, i tornen a
posar la barra i la vista de tothom. I **una app que s'acaba torna el vidre** —per les bones o perquè mor—,
perquè la pàgina següent i la barra no són seves per quedar-se-les.

| Crida | Torna | Notes |
|---|---|---|
| `ui.caps()` | `{width, height, levels, x, y}` | La grandària de la **vista**, els seus nivells (16) i **on està** dins del vidre. L'alçada és **el que queda sobre el teclat** mentre la banda és al vidre (0.84.0) |
| `ui.clear(level, [mode])` | `true` o `nil, error` | Tota la vista d'un nivell |
| `ui.rect(x, y, w, h, [level], [mode])` | `true` o `nil, error` | Un rectangle; `level` per defecte 0 (negre) |
| `ui.pixel(x, y, [level], [mode])` | `true` o `nil, error` | Un píxel |
| `ui.invert(x, y, w, h, [mode])` | `true` o `nil, error` | **Gira una caixa: la tinta es torna paper i el paper tinta**, nivell a nivell (el 5 es torna 10), que és el dibuix que diu «aquest és el que respondrà» en aquest vidre (0.178.0). És una primitiva i no una manera de pintar perquè el que es gira és **el que ja hi ha**: el paper d'una imatge, els seus grisos i la lletra del mig (docs/92) |
| `ui.present([mode])` | `true` o `nil, error` | **El sistema compon** el que ha canviat al buffer del vidre i demana el pintat. Sense `mode` **el sistema tria la qualitat de la pàgina** (0.179.5): `gl16` —setze nivells i cap flaix— i `gc16` quan el panell ja ha rebut tantes passades que li queda fantasma; **una qualitat que l'app va nomenar en un dibuix es respecta**. Amb `mode`, tot el que està pendent es pinta amb aquesta i prou. **Si el panell diu que no perquè encara mou la tinta del pintat anterior, la pàgina no es perd** (0.118.2): queda pendent i el sistema la torna a demanar tot sol quan el vidre la pot prendre, o sigui que un `nil` aquí **no** vol dir que la pàgina no s'arribarà a veure mai (docs/50, docs/92 §33) |
| `ui.damage()` | número | Quantes regions esperen un `present` |
| `ui.title(text)` | `true` o `nil, error` | El títol que surt **a la barra del sistema**. **No fa falta per posar-hi el nom de l'app**: el sistema ja hi escriu el `title` del `manifest.json` quan obre l'app. La crida és per dir-hi **una altra cosa** i per un motiu concret — el lector hi posa el llibre obert, el rellotge de fitxatge hi posa el dia— |
| `ui.rotation([degrees])` | el gir actual | **Gira la pàgina sencera**: 0, 90, 180 o 270. El vidre, la barra i els dits giren amb ella, i l'app rep `on_resize` amb la mida nova |
| `ui.full_screen([on])` | si té la pantalla sencera, amb argument; `true` o `nil, error`, sense | **La pantalla sencera** (0.136.0): la barra de les 48 files marxa i la vista és tot el vidre, `540 × 960` des de la fila 0, i l'app rep `on_resize`. Sense argument només respon si la té (la mateixa forma que `ui.rotation`). **El camí de tornada és del sistema i no de l'app**: tres tocs seguits al vidre tornen la barra, l'app no els sent mai i només rep `on_resize` amb la mida de tornada; i una app que s'acaba —per les bones o per un error— torna el vidre tota sola |
| `ui.idle()` | booleà | Si el pintat anterior ha acabat. **No diu que sí abans que el motor hagi tingut temps de marcar-se ocupat**, o sigui que es pot preguntar de seguida després del `present`. **I és el moment en què el sistema pinta el que el panell li va refusar** (0.118.2): esperar aquí una pàgina que no ha pujat és la manera de veure-la arribar (docs/50) |
| `ui.resize(w, h)` | `true` o `nil, error` | Una vista d'una altra mida: el sistema en fa una altra, blanca, i crida `on_resize` |
| `ui.canvas(w, h)` | **un objecte** o `nil, error` | Un control **amb vista pròpia**: un buffer i un lloc, que el sistema compon amb la resta quan l'app fa `ui.present` (vegeu a sota) |
| `ui.control(w, h)` | **un objecte** o `nil, error` | Un control **sense vista**: un marc amb fills que **no costa cap píxel** i que pinta al canvas que el conté, tallat al seu marc. Una graella, una caixa o un espaiador són això; amb `focusable = true` també entra al recorregut del dial i del tàctil, amb `on_activate` com a clic |
| `ui.label(w, h)` | **un objecte** o `nil, error` | Un control **sense vista** que és una línia de text: fons, color, cara, alineació i retall amb `...` (vegeu a sota) |
| `ui.button(w, h)` | **un objecte** o `nil, error` | Un control **sense vista** amb fons i vora que pot tenir un fill; el fill es posa amb `button:add` i es talla al marc del botó. **És enfocable i clicable** amb el dial i amb el tàctil (vegeu a sota) |
| `ui.checkbox(w, h)` | **un objecte** o `nil, error` | Un control **sense vista** que és una fila: un **quadrat amb una X a dins quan està marcat** (res quan no) i el lloc de les paraules, que són un fill. **És enfocable i clicable**, i el clic **gira l'estat** i avisa amb `on_change` (vegeu a sota) |
| `ui.progress(w, h)` | **un objecte** o `nil, error` | Una **barra**: fons, pista i el tros fet, tot del sistema. `value` és 0 a 100 i **un negatiu és «encara no ho sé»**. **No és enfocable** (3/11, 0.69.0) |
| `ui.slider(w, h)` | **un objecte** o `nil, error` | **Un número que mou el dial**: la pista, el botó i el número amb la seva unitat (`suffix`). **És enfocable**: les fletxes canvien `value` de `step` en `step` i l'app ho sap per `on_change(self, value)`. **Des de la 0.117.0 cal entrar-hi amb el botó central** (`editing`): fora, les fletxes segueixen caminant el dial; dins, són el número, i el centre les torna al sistema (3/11, 0.69.0, 0.117.0) |
| `ui.row(w, h)` | **un objecte** o `nil, error` | **Una fila de llista**: `title` a l'esquerra, `detail` a la dreta, `subtitle` a sota i el filet del separador. `marked` la inverteix. **No és enfocable** (3/11, 0.69.0) |
| `ui.panel(w, h, [flow])` | **un objecte** o `nil, error` | **El VBox o l'HBox de la pàgina**: `'vertical'` (per defecte), `'horizontal'` o `'overlay'`, amb `padding` i `gap`. Els fills es posen amb `panel:add(child)`, i cada cel·la diu **el seu percentatge del box amb `child.share`** (0.103.0) o la seva part del lloc lliure amb `child.weight` (3/11, 0.70.0) |
| `ui.tabs(w, h)` | **un objecte** o `nil, error` | Una **tira de pestanyes**: `items` (un array de paraules, vuit com a molt) i `index` (1 a n, i 0 = cap). **És enfocable, amb una parada per pestanya**: les fletxes van d'una pestanya a la següent, el centre tria la del cercle i `on_change(self, index)` avisa; `focus_index` diu quina té el cercle (3/11, 0.70.0 i 0.99.0). **I una tira que es pot tancar** (0.161.0): amb `closeable = true` cada pestanya porta una **`X` a la dreta, centrada verticalment**, i `tabs:tap(x, y)` diu què ha fet el dit —si era una creu crida **`on_close(self, index)`** i és l'app qui allibera el que hi havia darrere— i `on_change` si era el mot d'una pestanya |
| `ui.focus(control)` | **res** | **On és el dial**: l'app el posa en un control seu quan la seva pàgina s'ha tornat a construir i el que hi havia ja no hi és. Sense això la volta comença al primer control de l'arbre —la tira de pestanyes— i les fletxes canvien la pestanya per sempre (7/11, 0.97.4). Un control que no es pot enfocar no fa res |
| `ui.dots(w, h)` | **un objecte** o `nil, error` | **El paginador del sistema**: `pages`, `current`, `level` (la tinta, 0 per defecte) i `background`, amb la bola de la pàgina on ets plena i més gran (25) i les altres d'anella (19), unides per un pal de 3 (0.94.1); si les pàgines no hi caben, la finestra de les que hi caben i **tres boles més petites a cada cap** que no arriba (0.139.0). **No és enfocable**, i amb una sola pàgina dibuixa el seu paper i cap bola (0.176.0). **La caixa del disseny és de 37** (la bola de 25 i dotze d'aire a sota) i una caixa més baixa encongeix tota la tira amb el mateix factor: **el peu d'una llista i el d'una graella li demanen sempre aquesta caixa**, o sigui que el paginador és el mateix a tot arreu (0.176.0, docs/92 §27). **Accepta fins a 512 pàgines** (0.138.0): abans eren un byte amb un topall de 32, i un capítol d'un llibre en té més |
| `ui.list_entry(w, h)` | **un objecte** o `nil, error` | **Una fila d'una llista que l'app construeix** (0.104.0): paper, tinta, separador, `marked` i focus, i el contingut és un fill |
| `ui.grid_entry(w, h)` | **un objecte** o `nil, error` | **Una cel·la d'una graella que l'app construeix** (0.105.0): el mateix control que `ui.list_entry` sense la línia del separador, i el contingut és un fill (una icona amb el seu nom, un box de dues cel·les, una imatge) |
| `ui.list(w, h)` | **un objecte** o `nil, error` | **Files en pàgines i mai scroll**: `items` (una llista de taules amb `title`, `detail` i `subtitle`, o de cadenes, que són el títol), `row_height`, `page` i `marked` (la fila triada de tota la llista, 1 a n, o 0). **El peu és el paginador del sistema** (0.174.2, el mateix dibuix de `ui.dots` i **amb la mateixa caixa de 37 i la mateixa tinta que el control**: bola de 25, anelles de 19 i el pal que les enfila) i va **enganxat sota l'última fila** i no al fons de la caixa, i **les fletxes del dial canvien la pàgina** amb `on_page(self, page)` (3/11, 0.71.0; la caixa del disseny des de 0.176.0). **Un dit que camina de costat també la passa, i una passa més enllà de l'última fila visible també** (0.179.4) |
| `ui.textbox(w, h)` | **un objecte** o `nil, error` | **Una línia on es tecleja**, amb el cursor de bloc: `text`, `cursor` (quants bytes hi ha abans del cursor), `caret` (el bloc que el sistema fa parpellejar; l'app el llegeix), `mask` (puntets en lloc de lletres), `color`, `scale`, **`keyboard`** (`'alpha'` per defecte, `'numeric'`, o `false` si la caixa no vol teclat), **`readonly`** (una pàgina que es llegeix i no s'escriu: el cursor hi és i el teclat no puja mai, 0.116.0) i **`framed`** (si el control té vora; `false` la deixa fora i el paper de la pàgina arriba a la vora, 0.116.0). **És enfocable** (3/11, 0.72.0, 0.91.0, 0.92.0, 0.116.0) |
| `ui.keyboard()` | **un objecte del sistema** | **El teclat, que no és un control**: `kb.target = box` (on escriu), `kb:show('alpha'\|'numeric')` (exigeix una caixa de text), `kb:hide()`, `kb:height()` (la banda que pren) i `kb:visible()`. El teclat **pinta el sistema**, no l'app; des de 0.83.0 és un QWERTY de minúscules amb majúscules, barra d'espai i icones, i la banda fa quatre files en els dos jocs. **Des de 0.84.0 la vista de l'app és el que queda sobre la banda** (`ui.caps().height` baixa a 656 i l'app rep `on_resize`). **Des de 0.91.0 segueix el focus**: un dit sobre una caixa que pren tecles (`box.keyboard`) obre la banda i la caixa passa a ser el target; un dit fora la tanca (3/11, 0.72.0, 0.91.0). **Des de 0.107.0 les tecles fan 64 píxels d'alt** i la lletra ocupa l'alçada de la tecla, i el backspace té la icona del seu símbol |
| `ui.dialog(w, h)` | **un objecte** o `nil, error` | **Una targeta sobre la pàgina**: `title`, `text` (que **s'ajusta** en línies i no es talla), `note`, `accept` i `cancel`, `emphasis` (la porta que s'inverteix: 0 cap, 1 el cancel·lar, 2 l'acceptar) i `choice` (la porta on és el dial). El dit tria la porta que toca; el centre prem `on_accept`/`on_cancel` (3/11, 0.73.0) |
| `ui.sparkline(w, h)` | **un objecte** o `nil, error` | **La forma de les últimes mostres**: una barra per mostra (la més vella a l'esquerra), l'alçada contra **el pic de la finestra**, i `top` per a una segona sèrie que és part del mateix total (es dibuixa buida). No és enfocable (3/11, 0.74.0) |
| `ui.pie(w, h)` | **un objecte** o `nil, error` | Un **pastís de quatre porcions com a molt**: `slices` (valors; els angles surten de la suma) i el to més fosc; els altres tres són quatre passos més enllà, que és com l'app pot dibuixar-ne la llegenda (3/11, 0.74.0) |
| `ui.month(w, h)` | **un objecte** o `nil, error` | **Un mes de calendari**: `year`, `month` (1 a 12), `selected` i `today` (un dia del mes, o 0), `weekdays` (set noms curts, que són cosa de l'app **i van de dilluns a diumenge**), `color`, `dim` i `scale`. Set columnes i **sis setmanes sempre**; el triat s'inverteix i el d'avui porta marc. Les fletxes caminen els dies i el centre tria amb `on_pick(self, day)` (3/11, 0.75.0). **Des de la 0.170.0 la primera columna i els noms giren amb `time.week`** i el febrer té els dies que digui `time.leap` |
| `ui.menu(w, h)` | **un objecte** o `nil, error` | **El menú contextual**: una targeta d'opcions sobre la pàgina. `items` (un array de paraules, o de taules amb `label`, `tag`, `hidden` i `primary`, dotze com a molt), `index` (l'opció on és el dial, 1 a n), `title`, `color`, `dim` i `scale`. **Una opció que l'app no pot fer ara s'amaga** (`hidden`) i no es mostra morta; la de `primary` porta **un filet d'un píxel** i la del dial és una **banda de tinta d'extrem a extrem** (0.177.0: era una tecla amb relleu i la primària s'invertia). Les fletxes salten les amagades i el centre, o un dit, tria amb `on_pick(self, index)` (3/11, 0.76.0) |
| `ui.menu_bar(w, h)` | **un objecte** o `nil, error` | **Una barra de menús**: `items` (fins a cinc títols; cada un `{title=..., items={...}}`), `open` (el títol desplegat, 1 a n, o 0), `index` (l'entrada on és el dial), `bar_height`, `color`, `dim` i `scale`. Una entrada és una paraula, `{label=, checked=, enabled=}` o `'-'` per a una línia — **amb `checked` l'entrada porta la caixa, plena o buida, i sense el camp no en porta**. **Una elecció crida `on_pick(self, title, entry)` i el sistema plega el menú** (3/11, 0.77.0) |
| `ui.textbox_multiline(w, h)` | **un objecte** o `nil, error` | **Un camp de diverses línies**: `text`, `cursor` (bytes abans del cursor), `caret` (el bloc que el sistema fa parpellejar), `wrap` (si les línies llargues es parteixen), `top` (la primera fila que es veu, o `-1` perquè la trii el cursor), `color`, `background`, `border`, `scale`, **`keyboard`** (com el d'una línia), **`readonly`** (es llegeix i no s'escriu: el cursor hi és i el teclat no puja, 0.116.0) i **`framed`** (amb vora o sense, 0.116.0); **`box:rows()`** diu quantes files s'hi veuen i **`box:lines()`** quantes en té el text. **Si el text no hi cap, el cantó dret és una barra de desplaçament** que el dit mou si l'app ha registrat el gest amb `on_swipe`. **Retorn escriu un salt de línia** i el dit posa el cursor on toca (3/11, 0.78.0, 0.88.0, 0.91.0, 0.92.0, 0.116.0) |
| `ui.selector(w, h)` | **un objecte** o `nil, error` | **Un camp que s'escull i no s'escriu**: `label` (la pregunta, en el to `dim` i petit), `items` (les opcions), `index` (**la triada, 1 a n, i 0 = cap**, que és un estat de debò), `empty` (com es diu «cap» en la llengua de l'app) i `choosing` (si la llista és a sobre, que inverteix només la columna de la fletxa). **La llista no és al control**: el dit o el centre disparen `on_open(self)` i l'app hi posa un `ui.menu` (3/11, 0.79.0) |
| `ui.notification_bar(w, h)` | **un objecte** o `nil, error` | **La banda dels avisos**: `items` (cada avís és una paraula o `{title=, text=}`, **el més nou al final**), `color`, `dim`, `background`, `border` i `scale`. Les targetes s'apilen cap amunt amb **la més nova a baix**, tres com a molt; el que no hi cap **no es perd**: es compta amb un `+N`. La creu dispara `on_close(self, item)` i la targeta `on_pick(self, item)` (3/11, 0.80.0) |
| `ui.icon_grid(w, h)` | **un objecte** o `nil, error` | **Una graella de cel·les amb pàgines**: `items` (una paraula, o `{label=, mark=}`, on `mark` és la paraula curta de la caixa de la icona), `index` (la cel·la on és el dial, 1 a n, o 0), `marked` (la triada, que s'inverteix), `columns` (0 = el control decideix per l'amplada), `color`, `dim` i `scale`. **La pàgina segueix el cursor** i les fletxes caminen les cel·les; el centre i el dit trien amb `on_pick(self, index)` (3/11, 0.81.0). El peu és el **paginador del sistema** i **un dit que camina de costat passa la pàgina** (0.175.0, docs/30). **El cursor l'escriu el sistema** quan una cel·la pren el cercle (0.179.4) |
| `ui.frame(x, y, w, h, [level], [mode])` | `true` o `nil, error` | El **voltant** d'una caixa, d'un píxel, i el mig sense tocar |
| `ui.line(x0, y0, x1, y1, [level], [mode])` | `true` o `nil, error` | De punt a punt |
| `ui.circle(cx, cy, r, [level], [mode])` | `true` o `nil, error` | La **línia** del cercle |
| `ui.disc(cx, cy, r, [level], [mode])` | `true` o `nil, error` | El cercle **ple** |
| `ui.text(x, y, text, [scale], [level], [mode])` | `true` o `nil, error` | Un text amb la cara del canvas i, si no en té, amb la font del sistema, `scale` vegades la seva mida (1 a 16) |
| `ui.text_width(text, [scale])` | píxels | Quant fa de llarg el text amb la cara del sistema: el que centra un text en una caixa d'amplada fixa |
| `ui.line_height([scale])` | píxels | **L'alçada d'una línia** amb la cara del sistema (0.121.0). `ui.text` posa **el cim** de la línia a `y`, o sigui que un text centrat en una caixa és `y + (alçada − ui.line_height(scale)) / 2`. És l'altra meitat de `ui.text_width` i **no se suposa**, perquè les dues cares del sistema no fan la mateixa línia: la d'outlines del disc en fa onze píxels de línia per pas i la taula de cinc per set del flash en fa set (docs/92) |
| `ui.image(x, y, path, [w], [h], [mode], [mask])` | `true` o `nil, error` | **Una imatge d'un fitxer**: sense `w` ni `h` es dibuixa a la mida del fitxer; amb una mida, s'escala al més proper. **`mask`** (0.169.0, el setè) deixa els píxels que el PNG diu transparents tal com eren a sota en lloc de pintar-hi el blanc de l'alfa |
| `ui.image_info(path)` | `{format, width, height}` o `nil, error` | El que diu la capçalera del fitxer, **sense dibuixar-lo** |
| `ui.load_font(path)` | `font` o `nil, error` | **Una cara d'un fitxer**: del seu `res/` (`/res/x.lfont`, `.bdf`, `.pcf` o `.ttf`) o de la carpeta de cares del disc (`/fonts/dejavusans.ttf`, vegeu més avall, docs/70); el bloc de glifs és de l'app i compta al seu pressupost, i un `.ttf` és el fitxer sencer |
| `ui.font(font)` · `cv:font(font)` | `true` o `nil, error` | La cara de la vista i la d'un canvas; `nil` torna a la del sistema |
| `font:text_width(text, [scale])` | píxels | Quant fa de llarg una línia amb aquesta cara |
| `font:size()` | ample, alt, glifs | El que la cara diu de si mateixa |
| `font:release()` | `true` | Torna el bloc; el canvas que la feia servir torna a la del sistema |

- **`ui.idle()` és la pregunta del pintat, i el número surt de comptar des d'abans.** `ui.present` no
  espera el vidre: **torna de seguida** i la tinta continua pel seu compte, i `ui.idle()` és el que diu
  quan ha acabat. Per mesurar quant triga una pàgina es compta **des d'abans del `present`** fins que
  `idle()` diu que sí, i **no s'hi posa cap dormida pel mig**: una app que dorm 120 ms i després pregunta
  treu un 120 ms que és seu i no de la tinta (és el que va passar amb una pàgina sencera fins a la 0.19.2, i
  està escrit a `docs/95`, E-7). El motor del controlador no es marca ocupat en el mateix instant que rep
  la comanda, i per això el sistema no respon «ha acabat» fins passats uns mil·lisegons: és una guarda
  del conductor i no una mesura (`docs/50`).
- **Les imatges són fitxers i no taules de píxels a la memòria.** `ui.image` llegeix la capçalera, baixa les
  files una a una i les deixa al canvas, i el que queda al heap quan torna és el que hi havia abans. Els
  formats que es llegeixen avui són **PNG** (tipus de color 0, 2, 3, 4 i 6, profunditats 1 a 16, tots els
  filtres), **JPEG** (base i seqüencial estès, amb qualsevol factor de mostreig i marcadors de reinici),
  **BMP** (1, 4, 8, 16, 24 i 32 bits, amb paleta o sense, de dalt o de baix), **GIF** (87a i 89a, la primera
  imatge, LZW, transparència i entrellaçat), **ICO** (una imatge de BMP o un PNG a dins) i la família
  **netpbm** (`PBM`, `PGM` i `PPM`, en text i en cru). I **`ICS1`**, la mena de fitxer d'un cau d'aquest sistema
  (quatre bits per píxel, l'amplada sempre parell): el cau d'un llibre que escriu `epub.thumbnail` es llegeix
  aquí com qualsevol altra foto, que és el que fa que una app deixi una miniatura i una altra la pugui pintar
  (0.146.0). El que no es llegeix es diu amb un codi i no es dibuixa
  malament: un PNG entrellaçat (Adam7), un JPEG progressiu o aritmètic, un JPEG de quatre components (CMYK) i
  un BMP comprimit amb RLE són `E_UNSUPPORTED`; un fitxer que diu ser una imatge i no ho és, `E_IO`; un de més
  gran de dos megabytes o una imatge de més de 4 096 píxels per costat, `E_INVAL`/`E_NOSPC`.
- **El color és el del paper.** Una imatge de color es converteix a les setze tintes del vidre amb la
  **luminància** (0,299·R + 0,587·G + 0,114·B, en aritmètica entera) i un **dither ordenat** de 4×4: un
  gradient de vuit bits es veu com un gradient i no com setze franges, i un negre pur és negre a tot arreu. Una
  imatge amb transparència es compon **sobre el paper blanc**, que és el que hi ha sota seu (docs/50).
- **La mida que es demana és un escalat al més proper.** `ui.image(x, y, path, w, h)` dibuixa la imatge a
  `w`×`h`; una imatge de 96×72 a 480×360 ocupa el doble de píxels de la pàgina i no s'interpola, perquè setze
  nivells no tenen res a interpol·lar (docs/30).
- **Cada dibuix guarda la seva regió i, si l'app la nomena, la seva qualitat.** `ui.rect(0, 40, 60, 120, 7,
  'gc16')` apunta «del (0,40) al (60,160), i que quedi bé»; `ui.rect(0, 220, 40, 40, 0, 'du')` apunta «aquest
  quadre, i ràpid». **Una qualitat nomenada és de l'app i el sistema no la hi discuteix**; el que no en porta
  és d'un control que es dibuixa demanant `du`, que és el barat i no una promesa sobre el panell (0.179.5).
  `ui.present()` dona a les regions que no en porten la qualitat de la pàgina, i com que el controlador només
  pot pintar una cosa alhora, la segona espera que la primera acabi (docs/50).
- **El que es pot ajuntar s'ajunta.** Dues regions de la mateixa qualitat que es toquen (a dos píxels)
  es pinten d'un sol cop, i dues tirades de text en són una. Si una app dibuixa més de vuit regions
  diferents abans d'un `present`, totes es tornen una de sola, amb la qualitat més forta de les que hi
  havia, i aquesta regió inclou la darrera que s'ha dibuixat; `ui.damage()` ho diu abans de pagar-ho.
  **Abans era un `gc16`** (0.179.5): una pàgina de nou controls es convertia en el pintat que fa el flaix
  sense que ningú l'hagués demanat, i ara qui decideix és el `present` com a la resta de pàgines (docs/50).
- **Una pàgina és un sol pintat** (0.179.5): si el que ha canviat cobreix **mig vidre o més**, o són **quatre
  caixes o més**, es pinta d'un cop —el panell pinta una caixa cada vegada i cada passada espera la tinta, o
  sigui que quatre caixes són quatre passades per una pàgina—. Dues caixes són dues: el cercle que marxa d'un
  control i el que arriba a un altre no es toquen, i ajuntar-les pintaria l'aire del mig.
- **I el que s'ajunta no pot pintar l'aire del mig** (0.177.4). La unió de dues regions que no es toquen
  és un rectangle que també cobreix el que ningú no ha canviat, i això és tinta al lloc equivocat en un
  panell que fa un flaix quan s'hi escriu. Una cel·la de la primera fila d'una graella i la de la fila de
  sota es pintaven com **una sola regió de 500×246** —les dues files— per dues cel·les de 250×123; ara el
  que s'ajunta pot afegir una quarta part de la peça petita i prou, o sigui que les lletres d'una paraula
  segueixen essent un pintat i les bandes no.
- **Una peça que el mapa ja deu no s'hi torna a posar** (0.177.4): si el que s'apunta ja és dins d'una
  regió pendent, la regió queda amb la qualitat més forta de les dues i no s'hi afegeix res. És el que
  feia que el dibuix d'una icona es pintés **després** de la cel·la que ja el tenia.
- **La vista no és el vidre.** Un canvas té una mida i **un lloc** (`ui.caps().x`/`.y`), i tots dos són
  del sistema: el dia que dues apps comparteixin el vidre, o que el sistema es quedi una franja, la
  vista d'una app serà més petita i estarà en un altre lloc. El dibuix i el `present` no ho noten —les
  coordenades són sempre de la vista— i és el sistema qui les tradueix al vidre, tallant el que queda
  fora (docs/50).
- **La vista és de l'app i no del vidre.** `ui.clear`, `ui.rect` i `ui.pixel` escriuen en un canvas que
  el sistema fa quan l'app arrenca (**246 240 B al Paper**, 4 bits per píxel: 540 × 912, que és la pàgina
  de 540 × 960 menys les 48 files de la barra); res no arriba a la pantalla fins a `ui.present`. Una app que dibuixa una cantonada deixa la pàgina anterior al voltant, i una que vol
  la pàgina sencera comença amb `ui.clear(15)`.
- **Els nivells són 0 (negre) a 15 (blanc)**, que és el que tenen els quatre bits del panell (docs/50).
- **Els modes són els del propi xip**, i una app tria el preu: `du` (text i menús), `du4`, `gc16`
  (imatges de qualitat i netejar el fantasma), `gl16`, `glr16`, `gld16`, `a2` i `init` (la primera, la
  de dos segons). Per defecte, `du`. Un nom que no és a la taula es **refusa**; no s'arrodoneix a un
  altre. **Els temps del full de dades no són el que fa la placa**: mesurat l'1/11 (docs/95, E-7),
  `gc16` pinta i neteja (450 ms), `du` pinta però deixa rastre (120 ms), i **`du4` i `a2` no van pintar
  res** —`present` deia `true` i el vidre no canviava—. Fins que això no estigui resolt, pintar amb `du` i
  **esborrar amb `gc16` sobre la mateixa regió** és el que deixa el blanc net.
- **Una cara pot venir de quatre formats, i el fitxer mateix diu de quin.** `ui.load_font` mira la primera
  línia: `lunok-font 1` és el format propi —una línia de directives i els glifs en `#` i `.`— i
  `STARTFONT` és un **BDF**, el mapa de bits que llegia un servidor X: `FONTBOUNDINGBOX` amb la caixa, i
  per a cada `STARTCHAR` el seu `ENCODING` i les files del `BITMAP` en hexadecimal, amb el píxel de
  l'esquerra al bit de més pes. El tercer és un **PCF**, que és un BDF compilat: comença per quatre bytes
  que no són text (`01 66 63 70`), porta les taules indexades —mètriques, bitmaps i codificació— i els
  números de cada taula van en l'ordre de bytes que diu **aquella** taula (el directori, sempre petit). El
  que en surt és **el mateix bloc de sempre** —setze per setze, fins a 96 codis del 0x20 al 0x7F, en ordre— i
  per tant res del que dibuixa no canvia: la cara del fitxer es dibuixa al costat de la del sistema i surt
  del **mateix font** tant si ve d'un `.bdf` com d'un `.pcf` (el `.pcf` és el `.bdf` passat per `bdftopcf`, i
  la prova comprova que el bloc de les dues és idèntic byte a byte). Un fitxer que
  no comença per cap de les tres coses es refusa amb `E_INVAL` i el motiu (`not a font: neither a lunok
  font, a bdf nor a pcf`), i també un BDF o un PCF amb un glif de més de setze columnes
  (`a bdf glyph is up to sixteen columns wide`, `a pcf glyph is up to sixteen columns wide`). D'un PCF no es
  llegeix el que no se sap i no s'endevina: una codificació de dos bytes, una unitat d'escombrat més gran
  que un byte, o els bits al revés es refusen amb el seu motiu. Un **`.pcf.gz`** (el que tenen les
  distribucions de Linux) encara no s'obre: li falta el gzip, que és el mateix deflate amb una capçalera.
- **Els contorns d'un TrueType, llegits i dibuixats** (1/11, 0.41.0, `lib/ttf`). Un `.ttf` no és una taula de
  glifs sinó **un directori de taules**, i el que una pàgina necessita són set: `head` (la mida del cos i com
  són els offsets), `hhea` i `hmtx` (el pas de cada glif), `maxp` (quants glifs hi ha), `loca` i `glyf` (on
  comença cada contorn) i un `cmap` **de format 4**, que és el mapa de segments de l'ASCII. Un glif és una
  llista de contorns de punts: els que la corba travessa i els que la dobleguen, o sigui **quadràtiques**, que
  es tallen en sis trossos rectes —fins a **384 arestes** per glif, i un glif que en demani més es torna a
  tallar amb **quatre i després tres trossos per corba** abans de refusar-lo, perquè una cantonada es veu i una
  lletra que falta no és una lletra— i s'omplen per **regla de gir**: cada aresta que travessa el mig d'una fila
  diu cap a on va, i un píxel és tinta quan el compte a la seva esquerra no és zero —això és el que fa que el
  forat d'una lletra sigui un forat i no una segona taca—. El que no es llegeix es refusa amb el motiu:
  `OTTO` (contorns CFF, un altre format), un glif fet de peces (`kComposite`), un glif que no cap ni amb tres
  trossos per corba, i un codi que el mapa no porta (`kNoGlyph`). **I arriba a les apps**: `ui.load_font('/res/x.ttf')`
  el reconeix pels quatre primers bytes, i el que canvia respecte de les cares de bits és doble: **el bloc de
  la cara és el fitxer sencer** (comptat al pressupost de l'app, fins a 128 KB, i els 3 072 B del bloc de
  glifs no es gasten) i **`scale` vol dir la mida del cos en vuitens de píxel** (`kOutlineBody` = 8), o sigui que
  `text(x, y, s, 3)` és una línia de 24 píxels de cos. `font:text_width` suma els passos de cada glif en lloc
  de multiplicar una cel·la, `font:size()` diu `0, 8, 0` (l'amplada és de cada glif i el nombre de glifs és
  del fitxer). El lector de contorns llegeix el mateix que els de bits: un `.ttf` amb les mateixes sis lletres
  que la cara de prova dona els mateixos traços a dues, quatre i sis mides. Les cares de debò són al disc
  (`/fonts`): DejaVu Sans, Serif i Sans Mono, retallades a l'ASCII imprimible amb
  `pyftsubset --unicodes=U+0020-007E` (8 084 a 10 896 B cada una), amb la llicència al costat
  (`/fonts/LICENCE.txt`). Són tres perquè **tres és el que el sistema guarda alhora**, i la quarta —la
  negreta— hi entra quan l'app torna una de les altres: `release()` és el que fa lloc.
- **Quan el sistema canvia la mida de la vista** —una rotació del vidre, un dia, o la banda del teclat que hi
  entra i en surt (0.84.0)— l'app rep `on_resize(width, height)`. El sistema no sap què hi va: la feina de tornar a
  dibuixar és de l'app, i `cv:resize(w, h)` és el que necessita un `panel` per prendre la mida nova. Una app sense
  `on_resize` és una app legal; simplement no dibuixa mai la vista nova.
- **Quan el sistema torna una vista enrere** (0.163.0), l'app rep `on_back(depth)` amb **les vistes que li
  queden**: el pessic de gran a petit que ve de dalt de tot es del sistema, i el que fa és baixar una de
  les que l'app va apuntar amb `app.view_push()` i cridar aquest ganxo perquè la pàgina de sota torni al
  vidre. `depth = 0` vol dir que el pessic ha estat l'última pàgina que quedava, i **el ganxo no ha de
  tornar a baixar res**: el número ja l'ha baixat el sistema. Un `on_back` que cridi `app.view_pop()` deixa
  el comptador una vista per sota del que hi ha al vidre (docs/30).
```lua
local view = ui.caps()
ui.clear(15)                              -- la pàgina; el sistema ja l'ha deixada blanca al començar
ui.rect(0, 200, view.width, 40, 0, 'du')  -- un avís que es mou sovint, i ràpid: la qualitat és de l'app
ui.present()                              -- el que no en porta, la que demana la tinta de la pàgina
while not ui.idle() do sys.delay(100):await() end

function on_resize(width, height)
  -- la vista és una altra: torna a dibuixar-hi el que hi hagués
end
```

- **El text i les cares.** `ui.text` i `cv:text` dibuixen amb **la cara del canvas** i, si no en té cap,
  amb la font del sistema (5 × 7, **les dues caixes de lletres, 455 bytes de taula** a `lib/kernel/font.cpp`).
  Fins a 0.82.2 aquesta cara tenia només majúscules i plegava la minúscula a la seva capital; des de 0.83.0
  porta els dos alfabets (els codis `' '` a `'z'`), i per això una lletra escrita amb minúscula es veu
  minúscula a la caixa, a l'etiqueta i a l'editor. Una app pot carregar-ne d'altres amb `ui.load_font` des de
  `/res/fonts`, que són fitxers de text (vegeu la secció de les cares, més avall).
- Una app que dibuixa a la seva vista i espera que el vidre acabi té el patró sencer aquí: `ui.clear`,
  `cv:text`, `cv:present` i `ui.wait`.

### Les cares: un tipus de lletra que és un fitxer de text

**Una cara és una taula de glifs i una manera de llegir-los**, i la font del sistema és una cara com qualsevol
altra. Una app carrega una altra cara d'un fitxer amb `ui.load_font` —del seu propi `res/` o de `/fonts`, la
carpeta de cares del disc— i el que hi ha dins és **text pla** en el cas d'un `.lfont`: un glif per bloc i `#`
per tinta. El format, els motius pels quals un fitxer es refusa i com es paga la memòria són a
[`85 · Emulador`](85-emulador.md), que és on viuen les eines de treball; el que aquesta taula fixa és la crida
i el contracte.

- **Les cares del disc són una sola còpia per a tothom** (3/11, 0.68.0). Una cara de debò fa deu a vint
  kilobytes, i fins avui una app que en volia una se la posava a `apps/<id>/res/`: el mateix DejaVu viatjava
  dins de dues apps i el disc el pagava dues vegades. `/fonts` és el `res/fonts/` del disc vist des de
  l'arrel (docs/70), i una app **la llegeix i no hi escriu**, com `/apps`.
- **I des de la 0.174.1 `/fonts` és una porta de tres llocs i no un àlies** (l'amo: *«/fonts ha de ser com /apps
  ha de juntar /littlefs/fonts i /sd/fonts. En cas de el que veu l'app també suma el seu /apps/<id>/fonts»*):
  el disc —`res/fonts/`, que és on `ui::boot` busca la cara del sistema—, la targeta —`fonts/`, i la carpeta
  es crea si no hi és— i, **quan és una app qui llegeix, el seu `apps/<id>/fonts/`**. L'ordre és la de l'app,
  la del disc i la de la targeta, i un nom que dos llocs tenen es presenta dues vegades amb l'asterisc de
  `/apps` a la còpia de la targeta (`*dejavusans.ttf`), o sigui que **cada nom és un camí que es pot llegir**.
  Una app que porta la seva pròpia còpia d'una cara **la veu ella i la resta de la placa no**, i el llistat
  del disc des del prompt no la inclou: la carpeta és de l'app que corre (docs/70).
  El que hi ha avui: `dejavusans.ttf` (9 948 B), `dejavusans-bold.ttf` (**9 416 B**), `dejavusansmono.ttf`
  (8 084 B), `dejavuserif.ttf` (10 896 B), `dejavusans-latin1.ttf` (21 360 B) i `dejavuserif-latin1.ttf`
  (23 856 B) —les quatre primeres retallades a l'ASCII imprimible amb `pyftsubset`, i les dues `-latin1` amb
  `tools/ttf_latin1.py`, la del serif amb `--set latin1` (per als llibres) i la del sans amb `--set signs`
  (la del sistema, més avall)—, **una de cada format de bits que el nucli llegeix** —`sample-8x8.bdf` (862 B),
  `sample-8x8.pcf` (968 B) i `dejavusansmono-6x12.lfont` (9 017 B, fet amb `tools/ttf_to_lfont.py` a cos de
  deu píxels)—, i la llicència de DejaVu al costat, `/fonts/LICENCE.txt`. El lector i l'editor les fan
  servir i no en guarden cap còpia, i **les tres de bits hi són perquè la placa ensenya els quatre lectors
  en una sola pàgina**: és el que fa l'app `fonts` (docs/40, docs/90).

  **I una llibreria de vuit cares més, totes sota l'OFL** (11/11, 0.178.4): `literata-latin1.ttf`
  (31 120 B), `merriweather-latin1.ttf` (28 728 B), `atkinsonhyperlegible-latin1.ttf` (22 328 B),
  `opendyslexic-latin1.ttf` (20 660 B), `bitter-latin1.ttf` (26 000 B), `sourceserif-latin1.ttf`
  (33 600 B), `inter-latin1.ttf` (24 056 B) i `ibmplexsans-latin1.ttf` (22 444 B) — dues serif de
  llibre, dues de pantalla, una de lectura fàcil i una pensada per a la dislèxia—, **208 936 B les vuit** i
  les seves llicències a `/fonts/OFL.txt` (8 147 B, que són 217 083 amb les cares). Cada fitxer és un **subset** de la família original: el
  sistema llegeix una cara d'un fitxer de com a molt 128 KB (`kFontFileBytes`, més avall) i les originals
  van de 328 KB a 4,6 MB, o sigui que totes vuit són **Versions Modificades** de l'OFL i ho diuen al
  fitxer de llicència: retallades al joc de codis de les cares `-latin1` (`--set latin1`), les famílies
  variables fixades a la instància Regular (pes 400), i **3 de les 8 renombrades dins del fitxer** perquè
  la seva línia de copyright reserva el nom —Merriweather és `Lunok Serif`, IBM Plex Sans és `Lunok Sans`
  i OpenDyslexic és `Lunok Readable`—, que és el que demana la secció 3 de la llicència. El nom del
  fitxer sí que diu la família, que és el que una persona busca al llistat de `/fonts`.
  **OpenDyslexic és, a més, una conversió**: només es publica en CFF i aquest nucli refusa el CFF per nom
  (`a font of CFF outlines is not read`, `lib/ttf`), o sigui que els seus contorns s'han passat a TrueType
  abans de retallar-los (docs/85).

- **La cara del sistema porta també els signes d'una tecla** (8/11, 0.158.0). `dejavusans-bold.ttf` era
  l'ASCII imprimible i prou, i un signe com `÷`, `×` o `±` **no hi era**: el byte s'escrivia, la línia
  avançava i el vidre quedava buit. La cara es fa ara amb `python3 tools/ttf_latin1.py --ttf
  <DejaVuSans-Bold.ttf> --out data/res/fonts/dejavusans-bold.ttf --set signs`, i aquest joc és l'ASCII més
  nou codis: el `±` (`U+00B1`), el `×` (`U+00D7`), el `÷` (`U+00F7`), les quatre fletxes (`U+2190` a
  `U+2193`) i el retrocés (`U+232B`). Són **104 glifs**, **9 416 B** en comptes de 8 548 (868 B més de disc i
  de RAM mentre la cara del sistema és carregada), i el glif més gran de 78 punts i 5 contorns, dins del que
  el lector accepta (docs/75). Són exactament els signes que el projecte del paper coïa a les seves cares de
  mapa de bits (`tools/fontgen.py`, 216 codis amb les fletxes), i el motiu és el de sempre: la calculadora
  escriu el signe de la tecla i no el nom de l'operació (docs/92 §34). La prova que ho mesura és el cas de la
  calculadora de `test_apps`, que llegeix l'amplada dels quatre signes i una amplada de zero és un test
  vermell. El que no hi cap segueix sent un forat: la cara del sistema **no** porta les lletres amb accent,
  i una app que les vol carrega `/fonts/dejavusans-latin1.ttf` amb `ui.load_font`.

- **Una cara que ha de viure amb les altres es posa a la línia del sistema** (11/11, 0.178.4):
  `tools/ttf_latin1.py` té `--fit-line`, que escala la tinta de la cara a la línia que el nucli li dona i
  posa la línia de base on la tinta diu. Un pas de `scale` és un cos de **nou** píxels (`kOutlineBody`) i
  una línia d'**onze** (`kOutlineLine`), i la línia de base es calcula amb l'`ascent` del fitxer: una
  família amb la tinta d'1,86 em —OpenDyslexic— s'escapa de la caixa de l'etiqueta on es dibuixa i les
  línies d'una pàgina es trepitgen. Les cares de DejaVu no el necessiten (la seva tinta és 1,207 em, que és
  la línia); les vuit de la llibreria sí, i amb l'opció queden totes amb **1,212 em** de tinta dins dels 11
  píxels de la línia, la qual cosa es comprova amb `--check` sobre cada fitxer.

- **Una cara d'outlines porta els codis que porta el fitxer, i el byte és el codi** (7/11, 0.127.0). Fins a
  0.127.0 el dibuix d'un text saltava qualsevol byte per sobre de `0x7F` abans de preguntar-li res a la
  cara —dues línies, una a l'amplada i una altra al dibuix—, i això volia dir que un `é` no es podia
  escriure ni amb un fitxer que el tingués a dins. Avui qui decideix és la cara: si el fitxer porta el
  codi, la lletra avança i es pinta; si no, és un forat i la línia avança igual. El text d'aquest sistema,
  doncs, **és d'un byte per caràcter** —Latin-1 quan la cara ho és—, i convertir UTF-8 a aquest byte és
  feina de qui llegeix el text i no de qui el dibuixa.
- **La cara del llibre, i els glifs fets de peces** (7/11, 0.127.0). `dejavuserif-latin1.ttf` surt de
  `python3 tools/ttf_latin1.py --ttf <DejaVuSerif.ttf> --out data/res/fonts/dejavuserif-latin1.ttf`: 232
  glifs —l'ASCII, les lletres de `U+00A0` a `U+00FF`, els guions llargs, les cometes, els punts suspensius
  i el bullet—, **23 856 B**, i el glif més gran de 85 punts i 5 contorns (el lector en vol 256 i 64,
  docs/75). L'`e` amb accent és el que fa falta explicar: el DejaVu l'escriu com **un glif fet de peces**
  (la `e` i l'accent, cada un amb el seu dibuix i un desplaçament) i `lib/ttf` refusa això per nom, o sigui
  que el fitxer només retallat donava l'amplada bona i **cap tinta**. L'eina camina les peces amb
  `DecomposingRecordingPen` i el que en surt és el dibuix; amb els **55** compostos aplanats, l'`é` es
  pinta. La prova que ho mesura és el cas `test_a_latin1_face_draws_the_accented_letters` de `test_font`.

- **Una cara pot ser parcial**: un codi que no hi és es dibuixa com un forat i la línia avança igual, o sigui
  que una cara de deu xifres és una cara legal i el text del voltant no es desplaça.
- **Els bytes de `0x80` a `0x9F` són els caràcters de Windows-1252** (0.156.0): aquesta franja no té cap
  imatge a Latin-1 i és on la 1252 posa les cometes, els guions llargs, els punts suspensius i el bullet, que és
  la convenció amb què escriu una pàgina de llibre. El tokenitzador (`lib/epub`) ja convertia `&#150;` en un sol
  byte `0x96`, i `lib/ttf` responia «no hi ha aquest glif» i el guió **desapareixia**; ara el byte es tradueix al
  caràcter que vol dir abans de buscar-lo al fitxer (`ttf::glyph_of`), o sigui que un llibre en CP1252 i una
  entitat numèrica dibuixen el mateix. El que la cara no porti segueix sent un forat.
- **Els cinc codis que la 1252 deixa buits són les fletxes i el retrocés** (8/11, 0.158.0). La franja
  `0x80`–`0x9F` té un forat a `0x81`, `0x8D`, `0x8F`, `0x90` i `0x9D`, i allà no hi pot haver mai cap caràcter
  d'un fitxer; el sistema els dona als signes que un text **no es pot escriure d'una altra manera**, perquè un
  text és d'un byte per caràcter i la fletxa del Unicode (`U+2190`) no hi cap: `0x81` és `←`, `0x8D` és `→`,
  `0x8F` és `↑`, `0x90` és `↓` i `0x9D` és `⌫`. **El projecte del paper no ho necessitava** perquè el seu text
  era UTF-8 i les seves cares s'indexaven per punt de codi (`OLD/PaperOS/src/gfx/text.cpp` decodifica la
  cadena abans de buscar el glif); aquí el byte és el codi i prou, i aquests cinc són l'únic lloc on una fletxa
  pot viure (la taula és a `lib/ttf/src/ttf.cpp`, docs/92 §34).
- **I els codis per sota de l'espai són el lloc de les marques** (9/11, 0.173.0). Un byte de `0x01` a `0x1F`
  no és cap dibuix —fins a 0.172.2 l'amplada i el dibuix d'una cara d'outlines el saltaven—, però tampoc no
  és res que un text de debò pugui voler dir, i quan el text és d'un byte per caràcter és l'única cambra que
  cap pàgina ni cap llibre no farà servir mai. **Una cara d'outlines que porti una figura en un d'aquests
  codis la dibuixa**; la que no la porti respon un forat, exactament com abans, i per això cap text existent
  canvia ni de mida ni de tinta. Les cinc marques del rellotge de fitxatge —el play, la pausa, el stop, l'ull
  que obre la setmana i la fletxa enrere— són els codis `1` a `5` de `apps/timeclock/res/keys.ttf`, que fa
  `tools/make_keys.py` amb els glifs de `FontAwesome.otf` (els mateixos de què sortia el `keys.pof` del
  projecte del paper), i les pinta **el títol del botó** amb `botó:font(cara)` —la cara que el botó fa servir per
  mesurar i per dibuixar el seu `title`—, que és el que fa la tecla d'aquell projecte amb el seu `set_text()`
  (docs/45).
- **Qui converteix la cara en píxels és el nucli, i ningú més.** La cadena sencera és
  `cv:text`/`ui.text` → `luabind` (`text_at`) → `ui::canvas_text` (`lib/kernel/src/ui.cpp`) → `draw_text` →
  `font::runs(face_of(canvas.face), …)`, que camina els glifs i deixa cada tros com un rectangle al buffer
  del canvas. **El mapa de bits d'un tipus de lletra no surt mai del nucli**: a Lua arriben un número de mà
  (`ui.load_font`), tres números que descriuen la cara (`font:size`) i una amplada (`font:text_width`). Una
  app no processa cap font, i si ho intentés no tindria amb què: l'objecte `font` no té cap camp de glifs ni
  cap crida que els torni.
- **I la tinta d'una línia de llibre també és del nucli** (7/11, 0.129.0): `font::Ink` porta la **negreta**
  (el mateix rectangle un píxel més a la dreta, que és el que fa una cara d'un sol pes), la **cursiva** (un
  biaix fila a fila, un cinquè de l'altura del glif) i el **salt** del `sup`/`sub` (un terç de línia amunt,
  un cinquè avall, sense fer créixer la línia). `ui::canvas_text_styled` és la crida que ho fa sobre un
  canvas, i **no té porta a Lua**: qui la necessita és el lector de llibres, que és del sistema, i una app
  no ha de poder demanar una negreta que la cara que ha carregat no té (docs/46, A4 i D5).
- **La memòria és de l'app**: el bloc de glifs (`96 × 16 × 2 = 3 072` bytes) compta al pressupost
  (docs/75) i torna quan l'app crida `font:release()` o quan l'app s'acaba; un canvas que la feia servir
  torna a la font del sistema en comptes de llegir un bloc que ja no hi és.
- **Per mirar-ho**: el lector carrega una cara del disc i la dibuixa al costat de la del sistema, i diu a la
  consola els números de cada cara (mida, amplada de la línia i bytes del bloc).

### `ui.canvas`: el canvas d'una app, que és un control

**Un canvas és un buffer amb un lloc dins del que el conté.** La vista de l'app és el canvas arrel (el que
fan servir `ui.clear`, `ui.rect` i `ui.pixel`), i `ui.canvas(w, h)` en fa un altre **a dins de la vista**.
Els canvas es guarden en un **arbre**: quan l'app fa `ui.present`, cada canvas es compon al seu lloc dins
del seu pare, i el pare dins del seu, fins que la vista és una sola imatge al vidre. L'ordre és el
d'aparició: **un canvas fet després queda al davant** del que ja hi era.

| Mètode | Torna | Notes |
|---|---|---|
| `cv:rect(x, y, w, h, [level], [mode])` | `true` o `nil, error` | Com `ui.rect`, però al canvas propi |
| `cv:pixel(x, y, [level], [mode])` | `true` o `nil, error` | Un píxel |
| `cv:invert(x, y, w, h, [mode])` | `true` o `nil, error` | Com `ui.invert`, però al canvas propi (0.178.0): és el que fa servir el llançador perquè la icona es giri amb la cel·la on és el dial |
| `cv:capture(session, [offset])` | enter o `nil, error` | **Els píxels del canvas a la memòria de sessió** (0.179.0): quatre bits per píxel i l'amplada del canvas, o sigui el que `cv:blit` torna a pintar; torna quants bytes hi ha escrits i `E_NOMEM` si el bloc és més petit que el canvas. És com una app guarda una imatge que ja ha descodificat (docs/75) |
| `cv:blit(session, offset, x, y, w, h, [mode])` | `true` o `nil, error` | **Un bloc de píxels al canvas** (0.179.0): una còpia i no una descodificació —ni fitxer ni escala—, que és el que fa que el llançador pinti la icona d'una cel·la sense llegir res del disc. És la mateixa primitiva que fa servir una pàgina de llibre per a les seves làmines (docs/46) |
| `cv:clear([level], [mode])` | `true` o `nil, error` | Tot el canvas |
| `cv:place(x, y)` | `true` o `nil, error` | On seu dins del pare, en píxels del pare |
| `cv:resize(w, h)` | `true` o `nil, error` | **Una altra mida** per a un control que no té píxels propis (un panel, una etiqueta, una caixa de text): la mesura del grup; el que la necessita és un `on_resize` que torna a col·locar la pàgina (0.84.0). Un canvas **amb buffer** la refusa: els seus píxels són un bloc demanat amb una mida, i una altra mida és un altre canvas |
| `cv:rect`, `cv:frame`, `cv:line`, `cv:circle`, `cv:disc`, `cv:text` | `true` o `nil, error` | Les mateixes primitives que la vista, dibuixades al canvas propi |
| `cv:image(x, y, path[, w[, h[, mode[, mask]]]])` | `true` o `nil, error` | **Una imatge d'un fitxer, dins d'aquest canvas** (0.153.0), amb el mateix `mask` que `ui.image` (0.169.0): és el que fa servir el llançador per posar la icona d'una app dins la cel·la de la graella, i és `ui.image` amb el canvas propi com a destí i no la pàgina. El camí és el que una app pot llegir (docs/70), i `/apps/<id>/res/icon.png` ho és: un manifest és públic |
| `cv:add(fill)` | `true` o `nil, error` | El fill passa a estar **dins** d'aquest canvas, i és l'últim (la capa del davant). Un canvas té **un sol pare** |
| `cv:draw()` | `true` si ha dibuixat, `false` si no hi havia res | **Es dibuixa ell i després pregunta als fills**, en l'ordre en què es van afegir. **Si no té cap canvi, no fa res** |
| `cv:invalidate([x, y, w, h])` | `true` o `nil, error` | «Ha canviat el que ensenyo». Amb una caixa, **això** és el que s'ha de pintar; sense, tot el canvas |
| `ui.root()` | **l'objecte de la vista** | El mateix cada vegada: la vista també és un control, amb els seus `add`, `draw` i fills |
| `cv:parent()` | el pare, o `nil` | El canvas que el conté; `nil` vol dir que és la vista |
| `cv.on_draw` | el camp que posa l'app | `function(self)` que pinta el canvas propi; opcional. **A la vista** (`ui.root()`) és el que fa que el sistema li demani el forat que deixa una targeta en comptes d'omplir-lo de paper (0.173.3): el dibuix es crida **tallat en aquell requadre**, o sigui que ha de pintar el que li toca i no ha de fer res més quan un modal marxa (docs/30, docs/90) |
| `cv.children` | taula | Els fills, en l'ordre en què s'han afegit (la manté `cv:add`) |
| `cv:caps()` | `{width, height, x, y}` | La seva mida i el seu lloc |
| `cv:damage()` | número | Quantes regions seves esperen ser compostes |
| `cv:release()` | `true` | **Demana** que el control desaparegui: l'objecte queda mort de seguida, i el buffer es neteja i es torna **al final del proper `present`**, quan la pàgina ja ha estat tapada |

- **Un control és un canvas amb fills, i dibuixar-lo és preguntar-los.** `cv:draw()` crida el seu
  `on_draw(self)` i **després cada fill amb el seu `draw()`**, en l'ordre en què es van afegir: la vista
  d'un control es compon cridant els fills, i el que es va afegir després queda al davant. Un control
  sense `on_draw` és un control que només passa el torn. L'ordre dels fills és el de les capes, i el
  sistema composa amb el mateix ordre quan l'app fa `ui.present`.
- **Qui es redibuixa només pinta el que deu.** Mentre corre un `on_draw`, **totes les primitives d'aquell
  canvas es tallen a la caixa del canvi**: una app pot demanar el canvas sencer i el sistema li'n deixa
  pintar només el tros. I amb la qualitat que demana cada primitiva, que és la que viatja fins al vidre.
  Un control que pinta un dígit de rellotge que ha canviat paga la caixa del dígit, no la pàgina.
- **El senyal no puja sempre.** Un control que ha canviat el seu contingut **no fa repintar el pare**: la
  seva capa es compon a sobre del buffer del pare al `present`. Qui fa repintar el pare és el que deixa un
  forat: **moure** un control (el pare repinta exactament el rectangle que el fill ocupava) i **treure'l**
  (el pare repinta el lloc on era).
- **Un control que marxa es neteja abans de ser alliberat.** `cv:release()` **no allibera res**: marca el
  control (i els seus fills), diu al primer control que pinta **un sol rectangle —el del control que marxa**,
  no un per fill, perquè els dels fills són tots a dins del seu— i al final del `present` següent —quan
  aquella zona ja ha estat tapada— **esborra el buffer** (el deixa blanc) i torna el lloc. Així no queda cap
  resta fantasma ni al paper ni al buffer que rebrà un altre canvas. **El destructor no és a l'abast de
  Lua**: el que l'app crida és la petició, i l'alliberament el fa el sistema quan la pàgina és neta.
- **Un objecte que apunta a un control que ja no hi és no és un punter penjat.** L'objecte de Lua es guarda
  **l'espai i la generació**, i cada crida comprova els dos: si el control ha marxat, la crida respon
  `nil, E_INVAL` («aquest canvas ja no és a la pantalla»), `tostring` diu `canvas (gone)`, i el recolector
  no allibera res dues vegades. Un canvas nou pot ocupar aquell espai, però **no es pot escriure a través de
  l'objecte vell**, perquè la generació de l'espai ha avançat.
- **Un control amb res de nou no es dibuixa.** Cada canvas sap si té alguna cosa a refer: neix amb el
  senyal posat, el perd quan s'ha dibuixat, i el torna a tenir quan **es mou**, quan canvia de pare, quan
  se li assigna un `on_draw` nou o quan l'app crida `cv:invalidate()`. El senyal **puja als pares**,
  perquè un fill que s'ha mogut deixa els seus píxels dins del buffer del pare i és el pare qui els ha de
  tapar. `ui.present()` dibuixa el que ha canviat i prou: **`on_draw` no es crida si no hi ha res a
  fer**, i el segon `present` seguit d'un canvi qualsevol no pinta ni una regió.
- **Un sol pare.** `cv:add(fill)` treu el fill del canvas on era i el posa al final d'aquest; el seu
  `place(x, y)` passa a comptar-se en els píxels del pare nou. Un canvas no pot anar dins d'ell mateix
  ni dins d'un dels seus fills, i la vista de l'app no és filla de ningú.
- **Les primitives són les mateixes a la vista i a un canvas**, i cada una deixa **una sola regió de
  dany** —la caixa que ocupa— i no una per píxel: una línia diagonal no ha de semblar dos-cents dibuixos,
  o la llista de vuit s'omple i tot el canvas es pinta d'un sol cop (docs/50). Una línia recta
  horitzontal o vertical es pinta com un rectangle i és exacta; la resta és Bresenham. El cercle i el disc
  són l'algorisme del punt mitjà, sense coma flotant.
- **El color és el nivell**, de 0 (negre) a 15 (blanc), perquè és el que té el vidre: no hi ha color de
  debò en aquest panell i no se'n inventa cap (docs/50).
- **Un canvas té vora.** El que es dibuixa fora de la seva mida **no es pinta** —ni al canvas ni al pare,
  ni a la pàgina— i un rectangle més gran que el canvas queda tallat a la vora. És el mateix que fa un
  text amb la caixa on no pot entrar (`docs/50`).
- **El que costa és el que ha canviat.** Un canvas recorda què ha pintat des de l'última composició, i el
  pare només copia això: un canvas que no ha pintat res **no costa res**, i un text sobre una imatge no
  necessita cap transparència ni cap màscara (docs/90).
- **La qualitat viatja amb la tinta**: el `mode` amb què s'ha pintat una regió és el que s'usa quan la
  regió arriba al vidre, passi pels canvas que passi.
- **Un sol sostre**: els controls que hi caben són **els que la taula de la memòria extra
  permet** —milers, i el nombre el diu el log a l'arrencada (docs/75)—, i el de les vistes pròpies **ja no
  és un número del capçal** (0.174.5: eren **vuit** comptant la de l'app, i el llançador d'onze apps es
  quedava quatre caselles sense icona perquè la vuitena imatge responia `nil`). Un control sense vista no
  gasta ni un byte de píxel; si no hi ha lloc, `ui.canvas` o `ui.control` responen `nil` amb `E_NOMEM`, i
  el llançador **diu per què** una imatge no té caixa (`home: <id> has no picture: …`), perquè una graella
  de noms amb mitja dotzena d'icones que falten i cap línia sobre el que ha respost la memòria és una
  pàgina que amaga (docs/90).
- **Un control sense vista no és cap forat a l'arbre**: els seus fills es componen igual, amb el
  desplaçament de cada capa sumat, i el que ell dibuixa cau al canvas que el conté (el primer que en té) i
  **es talla al seu marc**: no pot pintar fora de la seva caixa. Un `UILabel` no necessita cap buffer.
- **Una vista feta de nou se'ls emporta**: si el sistema torna a fer la vista (una rotació, un `ui.resize`),
  els canvas de dins moren, i un objecte que l'app encara tingui **respon `nil, E_INVAL`** en lloc
  d'escriure al canvas que hagi ocupat el seu lloc.
- **Moure un canvas sí que torna a pintar el que deixa enrere**: el pare (o el primer control que pinta,
  si el pare no té buffer) rep exactament el rectangle que el fill ocupava, i el fill es compon al lloc nou.
- Dos canvas col·locats i compostos amb un sol `ui.present` és el que fa un editor de text amb el full i la
  barra de desplaçament a sobre: cada un té el seu buffer i el pare els compon.

### `UILabel`: una línia de text dins d'un marc

**Un label és un control sense vista** (`ui.label(w, h)`): el seu marc és la vora que talla el text i el
fons que pinta, i no gasta cap byte de píxel. Es posa amb `place`, s'afegeix a un pare amb `add` i el pinta
`ui.present` com qualsevol control.

| Camp | Valor | Què fa |
|---|---|---|
| `label.text` | cadena o `nil` | El text. Canviar-lo per un altre de diferent demana un redibuix |
| `label.color` | 0–15 | El nivell del text (0 negre, 15 blanc) |
| `label.background` | 0–15 (15 per defecte) | El nivell del fons del marc; el label és opac |
| `label.align` | `"left"`, `"center"`/`"centre"`, `"right"` | L'alineació horitzontal |
| `label.valign` | `"top"`, `"middle"`, `"bottom"` | L'alineació vertical |
| `label.scale` | 1–16 (1 per defecte) | La mida del cos, com el `scale` de `ui.text` |

- **La cara és la del canvas**: `label:font(f)` (o `label:font(nil)` per la del sistema) li posa una cara de
  `ui.load_font`; el label la fa servir per mesurar i per dibuixar. **I el control en guarda el número i no l'objecte**
  (0.177.1): si l'app dona la cara mentre el control encara l'ha de dibuixar —o deixa que el recollidor de Lua se
  l'emporti, que és el mateix—, el control passa a pintar amb la cara del sistema i no ho diu enlloc. Una cara que una
  pàgina fa servir viu tant com la pàgina, i es dona **després** dels controls (docs/75).
- **Si el text no hi cap, s'acaba amb `...`**: es queda el prefix més llarg que, amb els tres punts, cap a
  l'amplada del marc; si ni els punts hi caben, els punts són el que es dibuixa i el marc els talla.
- **Només es redibuixa quan cal**: el label neix amb el senyal de dibuixar-se, i després només quan canvia el
  text —o el color, el fons, l'alineació, la mida o la cara— i només si el valor és diferent. Un
  `ui.present` sense canvis no crida el seu `on_draw`. Quan la vista es torna a fer (una rotació o un
  `ui.resize`), l'app rep `on_resize` i torna a fer els labels, que es dibuixen una vegada.
- **El fons és opac a posta**: el control sense vista escriu al canvas que el conté i el seu marc no té
  transparència; un label sobre un panell fosc vol `label.background` del nivell del panell (docs/90).
- **Es pinta amb `du`** —és un control i no fa cap promesa sobre el panell— i **la qualitat de la pàgina la
  tria el sistema** al `present` (0.179.5): un label és una pàgina de text en miniatura i el que ha de quedar
  net és el que el sistema sap del vidre, no una suposició del control (docs/50, docs/90).
- `label.on_draw` és una funció del sistema; una app que la canviï deixa de tenir un label i té un control
  normal.

```lua
local title = ui.label(220, 32)
title.text = 'Hola, món'
title.align = 'center'
title.valign = 'middle'
title.color = 0
title.background = 15
title:place(20, 20)
title:font(face)          -- una cara de ui.load_font, o nil per la del sistema
ui.present()
```

El que fa un `UILabel` és el text d'una pàgina: canviar-ne el text o el color en dibuixa un, i un present
sense canvis no dibuixa res.

### `UIButton`: un marc amb un fill a dins

**Un botó és el primer control que és un contenidor amb un marc** (`ui.button(w, h)`): pinta un fons i una
vora, i el seu fill és un altre control —un `UILabel`, per exemple— que es posa amb `button:add(fill)` i
que es dibuixa **després** del botó i **tallat al mateix marc**. El fill no gasta cap byte propi i les
seves coordenades són les del botó, no les de la pàgina. **També és el primer control que rep una
persona**: el sistema el pot enfocar, prémer i clicar amb el dial de la placa o amb un dit al vidre, i
l'app només diu què vol dir el clic (docs/30, docs/90).

| Camp | Valor | Què fa |
|---|---|---|
| `button.background` | 0–15 (15 per defecte) | El nivell del fons del marc |
| `button.border` | 0–15 (0 per defecte) | El nivell de la vora |
| `button.title` | cadena o `nil` | **El mot del botó, pintat pel botó mateix** i centrat al que queda del marc: el mesura i el dibuixa amb la cara del control (`button:font(f)`), el talla amb `...` com un label, i **canvia de tinta amb l'estat** —el `color` quan està tranquil i el nivell del fons quan el focus o un dit l'inverteixen—, que és el que un fill posat a sobre no sap fer (0.173.0, docs/45) |
| `button.tag` | cadena o `nil` | Una etiqueta petita dins d'un requadre a l'esquerra del `title`, amb la mateixa tinta i la mateixa cara |
| `button.color` | 0–15 (0 per defecte) | El nivell del `title` i del `tag` |
| `button.scale` | 1–16 (1 per defecte) | La mida del `title`, com el `scale` d'un label |
| `button.primary` | `true` o `false` (fals per defecte) | El marc del botó fa **tres** píxels en comptes d'un |
| `button.pressed` | `true` o `false` (fals per defecte) | Mentre és cert, **el relleu s'inverteix** i el botó sembla enfonsat |
| `button.focused` | `true` o `false` (fals per defecte) | **L'estat del focus**, que porta el sistema: mentre és cert, el marc pinta **una pausa de dos píxels i una banda de set** —el doble marc del control seleccionat—, negra sobre fons clar i blanca sobre fons fosc |
| `button.focusable` | `true` o `false` (cert per defecte) | Si el dial i el tàctil poden aturar-s'hi. Un botó amb `false` continua pintant-se i pot rebre clics de l'app, però no entra al recorregut del sistema |

| Callback | Què fa |
|---|---|
| `button.on_click(self)` | El clic del centre o d'un dit a sobre. Si existeix, és el que es crida |
| `button.on_activate(self)` | El mateix clic quan no hi ha `on_click`; és el nom genèric per a un control que no és un botó |
| `button.on_focus(self, focused)` | Quan el dial o el tàctil mouen el focus: arriba (`true`) o se'n va (`false`). El primer control d'una pàgina ja neix enfocat, i per això el seu `on_focus` no es crida en crear-lo |
| `box.on_swipe(self, rows)` | **El gest d'una caixa de diverses línies** (0.88.0): un dit que recorre la caixa mou la finestra `rows` files —el sistema escriu `top` i presenta la pàgina— i l'app ho sent aquí, per dir on és dins del fitxer. Registrar-lo és el que encén el gest: sense aquesta funció el dit hi posa el cursor i no hi ha swipe |
| `box.on_move(self, cursor)` | **El cursor mogut per les tecles del dial** (0.116.0): un cop la caixa ha estat entrada amb el centre, les dues tecles del dial són el cursor —una lletra en una línia, una fila en un camp de diverses— i l'app ho sent aquí per dir on és (la línia de sota la pàgina). El sistema ja ha escrit `cursor` i `top` i pinta el bloc; l'app no ha de presentar |

- **Una cara de bits és de setze per setze, i una taula es pot fer d'una font de debò** (2/11, 0.55.0). La cel·la
  era de vuit per vuit —el que demanava una 5 × 7 doblada— i ara és de **setze per setze amb dues bytes per
  fila** (`kGlyphRowBytes`), perquè setze columnes no caben en un byte: el bloc d'una cara passa de 768 a
  **3 072 B** i el que no canvia és el camí que el dibuixa. I per tenir una lletra gran, arrodonida i amb
  majúscules i minúscules sense pagar un rasteritzador a cada pàgina, hi ha **`tools/ttf_to_lfont.py`**:
  converteix un `.ttf` en un `.lfont` a una mida triada (Pillow, el glif centrat a la cel·la perquè la taula és
  d'amplada fixa, el traç opcional) i refusa el que no cap a setze. El preu de la taula fixa: cada glif ocupa tota la cel·la, o sigui que un
  text amb una `.lfont` és més ample que el mateix text amb el `.ttf` (169 px contra 138 per a
  `Handgloves`), i per això les dues cares existeixen.

- **El marc del botó té pes** (3/11, 0.56.2). Tres píxels de vora en un botó més gran de 24 (dos en un
  de petit, un en un xip), el relleu **un píxel més prim que la vora** (d'un píxel en un xip), el forat de
  premut amb tantes anelles com la vora. Un filet lleuger llegeix com una caixa dibuixada al voltant d'una
  paraula; un marc amb pes llegeix com una cosa que un dit pot prémer (docs/50, docs/90).

- **I el focus és el doble marc més fort de la pàgina** (3/11, 0.56.2): després de la vora, els **dos píxels
  del relleu fan de pausa** i aleshores ve una **banda de set píxels** —la vora i quatre— del color que no es
  pot confondre, i el cos torna a dins. Un anell que només dobla la vora es llegeix com una vora més gruixuda
  i no com «aquest és el que té el focus»; la pausa és el que diu que hi ha dos elements i que el de dins és
  el focus. I com que el canvi de focus és un canvi d'estat de la pàgina, el que hi havia s'ha de **rentar**:
  un `du` hi deixa l'anell anterior a un quart de la tinta, que és un anell gris que ningú no ha demanat. Ho fa
  el sistema tot sol amb el present del focus (`gl16`, que neteja i no fa cap flaix) i una app que presenti
  sense dir la qualitat rep el mateix (docs/50, docs/90).

- **La pàgina dels botons ja no porta una taula de bits** (3/11, 0.56.2): una pàgina de botons carrega
  **Lato Black** d'un `.ttf` d'outlines retallat a l'ascii imprimible (`pyftsubset --unicodes=U+0020-007E`,
  **19 340 B**, la llicència OFL al costat, `/res/LICENCE-lato.txt`) i dibuixa els vuit labels a **24 px**
  (`scale = 3`), el títol a 32 i les notes a 16, amb el mateix tipus de lletra. És una **rodona** de debò —el
  `g` té noranta-un punts i és el glif que va fer falta tallar amb menys trossos de corba— i una cara
  d'outlines és **proporcional**: les amplades surten de la taula del fitxer i el text no té la respiració de
  la cel·la fixa; el preu és que el nucli ha de dibuixar cada glif, i per això les dues cares conviuen
  (docs/85).

- **El botó té forma i volum, i el focus no es pot perdre de vista** (2/11, 0.51.0): el marc és una **caixa
  amb els cantons rodons** (dotze píxels, i mai més d'un terç del costat més petit — un botó que és tot cantó
  és una pastilla i no un botó), amb una vora de dos píxels —tres en un botó de pàgina des de la 0.56.2—. A
  dins hi ha el **relleu**: una aresta clara a dalt i a l'esquerra i una ombra a baix i a la dreta, i **mentre
  es prem les dues canvien de lloc**, que és tota la diferència entre un botó que surt i un que s'enfonsa. **I
  el relleu és paper i tinta i no un gris** (0.85.4): un cos clar no té aresta més clara i la seva ombra és
  tinta, i un cos fosc té lluentor de paper i cap ombra —una aresta de grisos (`fons ± 4`) era un control que
  demanava el rentat de setze nivells i que deixava un fantasma gris quan es movia—. El **focus és un anell**
  cinc passes endins, en el nivell que no es pot confondre: negre sobre un botó clar i blanc sobre un de fosc.
  És un píxel de tinta i no un degradat, perquè el que es veu és una aresta. El gruix de la vora i el de
  l'anell són els de la 0.56.2, més amunt.
- **El fill és un control de debò**: `button:add(label)` el treu del pare anterior i el posa dins del botó;
  `label:place(x, y)` passa a comptar-se en píxels del botó. `button:add` i `button:place` són els
  mateixos mètodes de qualsevol canvas (docs/30).
- **El marc és opac i el fill va a sobre**: el botó s'ha de pintar abans que el fill, i el fill s'ha de
  tornar a dibuixar quan el marc canvia; si no, el fons nou taparia el label. El nucli ho fa sol: quan un
  `UIButton` es redibuixa, invalida els seus fills i el present els torna a pintar. Un fill amb fons opac
  —com un `UILabel`— ha de portar el mateix nivell que `button.background` si es vol que el botó es vegi
  sencer.
- **El sistema és qui té el focus i el `pressed`**: el primer control enfocable d'una pàgina neix amb el
  focus; `amunt`/`esquerra` van a l'anterior, `avall`/`dreta` al següent i `centre` és Enter, amb la
  volta completa. El sistema llegeix el `spot` del HAL i no el número de la tecla, i una placa d'una sola
  tecla només té el centre. Si l'app canvia `pressed` per ensenyar un estat, el sistema el torna a deixar
  anar quan el clic acaba; `pressed` és estat i no un esdeveniment.
- **El tàctil és el mateix event**: baixar un dit sobre el control l'enfoca i el deixa premut, moure's
  fora l'aixeca i tornar-hi el torna a prémer, i aixecar-lo a dins el clica. Les coordenades són les de la
  vista —les mateixes que dona `touch.read()`— perquè `touch::poll` ja les ha girades i desplaçades abans
  de passar-les al control; el primer dit és el que mana.
- **Un control que no és un botó també pot ser polsable**: qualsevol control amb `focusable = true` entra
  al mateix recorregut i pot rebre `on_activate`. Com que un `ui.control` no té un dibuix de focus propi,
  l'app el pot pintar mirant `focused` al seu `on_draw` (docs/90).
- **Una pàgina amb controls no es tanca amb tres tocs del centre**: el centre és Enter, i el sistema no
  pot tenir les dues coses alhora. La sortida d'emergència segueix sent `ESC` i un control `Quit` de
  l'app (docs/30).
- **El botó no té text propi**: el text és un `UILabel` fill, amb la seva cara, el seu color i la seva
  mida. Això és el que permet un botó amb dues línies, una icona dibuixada o qualsevol altre control a
  dins.
- `button.on_draw` és una funció del sistema; si una app la canvia, deixa de tenir el marc de botó i té un
  control normal amb el seu propi dibuix.

```lua
local button = ui.button(240, 100)
button.background = 0
button.border = 15
button:place(20, 20)

local label = ui.label(220, 80)
label.text = 'A label inside'
label.color = 15
label.background = 0
label:place(10, 10)
button:add(label)

button.on_focus = function(self, on)
  console.print('focus: ' .. tostring(on))
end
button.on_click = function(self)
  console.print('clicked')
end

ui.present()
```

Vuit labels de la mateixa cara —Lato Black, la del títol i les notes— a **tres tamanys** (16, 24 i 32 px)
dins de **vuit botons blancs** és el que fa una pàgina de botons. El vuitè es prem i es deixa anar amb
un `gc16` que neteja el forat, el focus es renta quan es mou, i els comptes de la consola mostren que un botó
que canvia també torna a dibuixar el seu fill.

### `UICheckbox`: un quadrat, una X i l'estat

**Un checkbox és una fila** (`ui.checkbox(w, h)`): una **caixa quadrada** a l'esquerra i, a la dreta, el lloc de
les paraules que diuen què es marca. Les paraules són un `UILabel` fill que es posa amb `cb:add(etiqueta)`,
exactament com el text d'un botó, i es col·loca amb **`cb:box()`** —els píxels que ocupa la caixa, que són del
nucli i no un camp—. El marc del control és la fila i el que dibuixa és la caixa que hi ha a dins (docs/30,
docs/90).

| Camp | Valor | Què fa |
|---|---|---|
| `checkbox.background` | 0–15 (15 per defecte) | El nivell de la fila, que és **opaca**: les paraules s'hi posen a sobre |
| `checkbox.border` | 0–15 (0 per defecte) | El nivell de la vora de la caixa |
| `checkbox.mark` | 0–15 (0 per defecte) | El nivell de la **X**, que és un color propi perquè una X negra en un quadrat negre és un checkbox que no diu res |
| `checkbox.checked` | `true` o `false` (fals per defecte) | **L'estat**: l'app el llegeix i l'escriu, i el clic del sistema el gira |
| `checkbox.pressed` | `true` o `false` | **No fa res**: el que fa el dit és girar l'estat, i l'estat ja és la X |
| `checkbox.focused` | `true` o `false` | L'estat del focus, que porta el sistema: el quadrat s'**inverteix** (marc de paper, dins de tinta i la X de paper) |
| `checkbox.focusable` | `true` o `false` (cert per defecte) | Si el dial i el tàctil s'hi aturen |

| Callback | Què fa |
|---|---|
| `checkbox.on_change(self, checked)` | El clic, **després** de girar l'estat: `checked` és el que hi ha ara |
| `checkbox.on_focus(self, focused)` | El mateix que el d'un botó: el focus arriba i se'n va |

- **El clic vol dir una cosa i la fa el sistema.** Un checkbox **no crida `on_activate` ni `on_click`**, perquè
  el seu clic ja té un significat. El camp `checked` ja val el que ha de valer quan arriba `on_change`, i per
  això una app que només escolta no pot deixar el dibuix i l'estat dient coses diferents. Una app que havia de
  girar-la ella seria una app que se'n pot oblidar, i un checkbox que no marca la pàgina és un checkbox que ha
  mentit (docs/30, docs/90).
- **El quadrat és del nucli i es mesura amb `cb:box()`.** El marge de la fila és de quatre píxels a dalt i a baix,
  el **marc és de dos píxels** a tot arreu i els **cantons són cantons** (no són arrodonits): és el que fa que es
  llegeixi com un quadrat i no com un xip. Una fila massa curta en gasta menys de marge, i una de menys de sis no
  té quadrat.
- **La X és el que diu l'estat** (0.102.1, l'amo: *«un quadrat i una X dins si està activat o res si no ho està»*):
  dues línies de dos píxels d'un cantó de dins al de davant, amb un píxel d'aire per no tocar el marc. Si el quadrat
  és massa petit per dibuixar-la (menys de deu píxels) el quadrat es queda sol.
- **El focus inverteix el quadrat** —marc de paper, dins de tinta i la X de paper—, que és el mateix senyal que dona
  un botó i l'única cosa que es veu en un vidre de dos tons i mig. La fila és de l'app, i una app que vulgui que les
  paraules diguin el focus les pot pintar al seu `on_focus`; si el quadrat va dins d'un botó amb el cercle, el fill
  intercanvia els seus tons sol (0.101.0, docs/30, docs/90).
- **La fila és opaca, com el marc d'un botó**: les paraules es componen després del checkbox i queden tapades
  pel fons nou, o sigui que un checkbox que es redibuixa **invalida els seus fills** i el present els torna a
  pintar (docs/30, docs/90).
- **Es pinta amb `du`** i el canvi el renta el present que el sistema tria (0.179.5): una X és poca tinta, però un
  quadrat que s'ha mogut o invertit deixa el fantasma d'abans i el que el neteja és el que el sistema sap del
  vidre (docs/50, docs/90).
- `checkbox.on_draw` és una funció del sistema; si una app la canvia, deixa de tenir un checkbox i té un control
  normal amb el seu propi dibuix.

```lua
local cb = ui.checkbox(300, 48)
cb:place(20, 80)
cb.checked = true

local words = ui.label(230, 40)
words.text = 'The clock'
words.scale = 2
words.valign = 'middle'
words:place(cb:box() + 12, 4)   -- després de la caixa, que és el que diu cb:box()
cb:add(words)

cb.on_change = function(self, on)
  console.print('clock ' .. tostring(on))
end
ui.present()
```

Una pàgina de cinc files: **les paraules dins de cada fila**, una fila que **neix marcada**, una que el dial
**no pot tocar** (`focusable = false`, i el recorregut del sistema la salta), una de fosca amb la X i la vora
blanques, i una línia que compta quantes estan enceses. Cada canvi diu a la
consola el nom i l'estat nou, i la pàgina es renta perquè l'anell que s'ha mogut no hi deixi el rastre: la
qualitat la tria el `present` del sistema (0.179.5).

### `ui.progress`, `ui.slider` i `ui.row`: el que dibuixa el sistema (0.69.0)

**Cap control no es dibuixa en Lua.** Una app diu els camps, col·loca el control i decideix quan la pàgina va al
vidre; cada píxel —la pista, el botó, el número, la X, el marc invertit del focus— el posa
`lib/kernel/src/ui.cpp`. És el que fa que dos apps tinguin els mateixos controls, i el que permet que una pàgina
de controls costi un repintat i no un per control (docs/30, docs/90).

| Control | Camps | Enfocable |
|---|---|---|
| `ui.progress(w, h)` | `value` (0 a 100, o **−1 = encara no ho sé**), `background`, `border`, `track` (la pista), `fill` | **no**: una barra no és una cosa que es prem |
| `ui.slider(w, h)` | `value`, `min`, `max`, `step`, `suffix` (el `%`), `background`, `border`, `track`, `fill`, `text`, `scale`, `editing` | **sí**: un cop entrat amb el centre, les fletxes mouen el número i `on_change(self, value)` avisa, amb el camp ja escrit; fora, les fletxes caminen el dial |
| `ui.row(w, h)` | `title`, `detail`, `subtitle`, `text`, `dim`, `background`, `scale`, `marked`, `separator` | **no**: una fila és el dibuix d'una llista |

- **El número d'un slider és del control**: mentre té el focus, les fletxes el mouen de `step` en `step` i al
  final del recorregut **la tecla torna al recorregut del dial** —que és el que impedeix que una pàgina amb un
  slider sigui un cul-de-sac—, i això ho fa el sistema, no l'app (docs/30).
- **Una barra que diu alguna cosa sempre ensenya alguna cosa**: un per cent d'una barra ampla és una punta
  rodona i no un píxel de res, i un `value` negatiu deixa la pista sola perquè una pàgina que ensenya la barra
  plena abans de saber res és una pàgina que menteix.
- **Una fila marcada s'inverteix, no s'engriseix**: a setze nivells, «aquesta és la que està triada» ho diu la
  tinta i el paper canviats. Cada línia es retalla amb `...` a la seva columna, i el detall va a la dreta amb
  l'amplada que li queda, o sigui que el nom i la mida no es trepitgen mai.

```lua
local bar = ui.progress(300, 28)
bar.value = 50
bar:place(20, 60)

local s = ui.slider(300, 56)
s.value = 30
s.suffix = '%'
s.step = 5
s.scale = 3
s:place(20, 110)
s.on_change = function(self, value) console.print('slider ' .. value) end

local row = ui.row(300, 64)
row.title = 'Res'
row.detail = '1 033 B'
row.subtitle = 'the sample files of the disk'
row.separator = true
row.marked = true
row:place(20, 190)
ui.present()
```

Una barra a mig fer, un slider que el dial mou i quatre files de llista amb la tercera marcada són els tres
controls alhora. Les proves que mesuren la conseqüència són `test_a_progress_bar_fills_a_prefix_of_its_track`,
`test_a_slider_puts_the_knob_where_the_value_says` i
`test_a_row_puts_its_detail_on_the_right_and_inverts_when_it_is_marked` a `test_ui`, i
`test_the_dial_moves_the_value_of_a_slider` a `test_apps` (docs/80).

### `ui.panel`: la pàgina és una declaració (0.70.0)

**Un panel és un lloc, no un dibuix**: no té buffer, no ocupa cap píxel, i l'únic que fa és donar a cada fill el
seu marc. Tres fluxos: `'vertical'` (una columna: cada fill sota l'anterior, amb el `gap` al mig), `'horizontal'`
(una fila: cada fill a la dreta de l'anterior) i `'overlay'` (tots al mateix punt, l'últim afegit davant).

- **El `padding` és de dins del panel** i el `gap` és el que hi ha entre dos fills; cap dels dos és una constant del
  sistema, perquè una pàgina de quatre línies i una de dues no volen el mateix aire.
- **Un fill és tan gran com el que va demanar** (`ui.label(200, 20)`), i **creuat al flux** és tan gran com el panel:
  una línia d'una columna és tan ampla com la columna. Això és el que fa que l'app no hagi de dir cap coordenada.
- **`child.share` és un percentatge del box** (0.103.0): `1` a `100`, i és **de dins del box** i no del que sobra,
  o sigui que una cel·la que diu `50` és la meitat del panel tant si els germans són fixos, amb pes o no hi són. És
  la manera de dir un VBox de dues meitats o un HBox de 25/75 sense comptar el que han demanat els altres.
- **`child.weight` parteix el lloc que sobra**: les cel·les amb pes 0 es queden la mida que tenen, les que tenen
  `share` ja s'han endut el seu tros, i el que queda es reparteix pel pes amb **el darrer que pren el romanent** (o cent
  píxels d'arrodoniment deixarien una tira de pàgina al fons del panel). Un pes de 0 a 15.
- **Tres maneres de col·locar-se, i cada cel·la tria la seva**: `share` (el percentatge), `weight` (el que sobra) o
  cap dels dos (la mida amb què es va fer). No cal dir-ho a totes: una cel·la fixa conviu amb una de percentatge i
  amb una de pes.
- **Quan es fa el repartiment?** Quan el panel es dibuixa. Per això una pàgina girada, una vista d'una altra mida o
  un fill afegit de nou es col·loquen sols: l'app no ha de tornar a calcular res.
- **El fill que no és del panel no es toca**: si un control s'ha mogut a un altre lloc, el panel el salta, o deixaria
  un forat on no hi ha ningú.

```lua
local page = ui.panel(300, 400, 'vertical')
page:place(20, 60)
page.padding = 12
page.gap = 8

local title = ui.label(10, 40)
title.text = 'Alarms'
title.scale = 3
page:add(title)

local body = ui.label(10, 100)
body.text = 'Nothing to wake up for'
body.weight = 1          -- es queda el lloc que sobra
page:add(body)

-- **I una cel·la que vol el 40 % del box**, sigui quina sigui la mida de les altres:
local half = ui.panel(10, 10, 'vertical')
half.share = 40
page:add(half)
ui.present()
```

### `ui.tabs` i `ui.dots`: on ets a la pàgina (0.70.0)

- **`ui.tabs`**: `items` és un array de paraules (vuit com a molt) i `index` és quina està triada, 1 a n; **0 vol dir
  cap**. Les columnes són iguals, i **la triada és la que no té la línia a sota**: a dos tons no hi ha negreta ni
  color, i treure la línia és el que diu «ets aquí» (el que el vell va aprendre a `tab_strip.h:44`). **Cada pestanya
  és una parada del dial** (0.99.0, com al projecte del paper, on les pestanyes són botons): les fletxes entren a la
  tira i van d'una pestanya a la següent, i surten de la tira per qualsevol dels dos caps; **el centre tria la
  pestanya on és el cercle**, i llavors l'app ho sap per `on_change(self, index)`. **La pestanya del cercle es pinta
  invertida** —la cara pren la tinta i la paraula el paper, i la línia i el marc del darrere van a l'inrevés—, que és
  el mateix senyal que fa servir qualsevol control enfocat. `focus_index` diu quina és (1 a n, 0 = el cercle és en
  un altre control) i una app pot llegir-lo.
- **`ui.dots`**: `pages` i `current`, i el de la pàgina on ets **és una bola plena i més gran**, mentre que els altres
  són **anelles amb paper a dins**, tots enfilats en un **pal** que només es veu a l'aire entre dues boles (0.94.1). Són
  dos tons i prou —negre i paper—, i per això la tira es pot pintar i esborrar amb la forma d'ona binària; una tira de
  grisos s'hi va quedar esborronada al projecte vell. **Amb una sola pàgina no dibuixa cap bola** —un punt sol no diu res i
  costaria un repintat del panell per dir-ho— **però sí que pinta el seu paper** (0.176.0): és un control i el seu requadre
  s'ha de cobrir, perquè un control que no pinta res no esborra el que hi havia (el lector d'un capítol d'una sola pàgina
  hi trobava les boles del capítol anterior, docs/90). El lloc de cada bola és el de la més gran, o la tira es mouria de
  costat quan la gran canvia de punta. **Si les pàgines no hi caben**, la tira en dibuixa tantes com hi caben i la finestra
  d'elles que conté la pàgina on ets: el comptador no surt mai de la seva caixa i la pàgina que llegeixes és sempre una
  de les boles. **I als caps on la finestra no arriba a les pàgines de debò, la tanca amb tres boles que es fan més
  petites** (0.139.0): **16, 12 i 8** píxels al costat de la bola de 25 —2/3, 1/2 i 1/3 d'ella—, amb **8** d'aire entre
  dues i **16** de separació de la tira, que és el dibuix del paginador del paper (la seva tira del peu escriu 8, 6 i 4
  al costat d'una bola de 12, i les proporcions són les mateixes; aquella bola de 12 era del projecte del qual es va
  portar el dibuix, i no la tira que dibuixava el lector d'aquest arbre, que és un `ui.dots` des de la 0.176.0).
  Aquestes marques són el que diu que hi ha moltes més pàgines de les que es poden dibuixar; una finestra que només
  llisca no ho diu a ningú. Una caixa massa estreta per a
  les tres marques (a 37 píxels d'alçada en vol 68 a cada cap) es queda amb la finestra de sempre, sense marques.
  **El nombre de pàgines va d'1 a 512** (0.138.0): era un byte amb un topall de 32, que és menys que les
  pàgines d'un capítol de debò.

  **Les mides són les del projecte del paper, exactes** (0.112.0): bola de **19** píxels per a les pàgines que no són
  obertes, amb una anella de **2** píxels de tinta i paper a dins; bola de **25** per a la que es llegeix, **sòlida**, i
  és l'amplada de **totes** les caselles; **7** píxels d'aire entre dues boles i un **pal de 3** que les enfila. La caixa
  que demana el disseny és de **37** píxels —la bola gran i **12** d'aire a sota, perquè la tira no s'enganxi a la vora—
  i la tira es pinta **a dalt de la seva caixa**, amb el que queda per sota. Una caixa més baixa que 37 encongeix tota la
  tira amb el mateix factor (l'aspecte no canvia); una de més alta li deixa l'aire a sota. **L'amplada sí que és la de
  la pàgina**: la tira es centra dins del que li donen i es talla al marc, o sigui que una app ha de donar-li l'amplada
  on vol que es vegi (a les apps del disc: `ui.dots(view.width - 2 * 12, 37)`).

  **I el paginador és el mateix a tot arreu, que és el que el fa entendre** (0.176.0). La caixa del disseny i la tinta
  del control no són una recomanació per a qui el fa servir: **el peu d'una llista i el d'una graella demanen aquesta
  caixa de 37 i la tinta del control** (`color`/`text`, negre) i no el `dim` d'una paraula secundària. Abans no era
  així —una llista donava a la tira un terç de la fila, amb un terra de 24 i un sostre de 37 (o sigui una bola de 20 a 33
  segons la fila), una graella una banda de 28 (bola de 24) i un `ui.dots` una bola de 25, i la de la graella anava en
  gris—, i l'amo ho va veure de seguida: *«per qué és tant gris el paginador? no és un UIControl? per qué cada cop
  que n'uses un és diferent? … el paginador ha de ser igual a tot arreu, si no no s'entén que és un control
  paginador»*. **Un sol dibuix, una sola mida, una sola tinta**; una app que li doni una caixa més baixa és l'única que
  el fa petit, i ho fa a posta (docs/92 §27).
- Cap dels dos no és un dibuix de l'app: són controls del nucli, com la resta (docs/92 §10).

### `ui.focus`: on és el dial quan la pàgina es torna a construir (0.97.4)

**Una pàgina que es construeix de nou perd el control on era el dial**, i la volta comença
al primer control de l'arbre, que en una app amb una tira de pestanyes és la primera pestanya.
Fins a 0.99.0 això era una trampa: la tira era **una sola** parada, les fletxes la recorrien
canviant la pestanya, la pàgina es tornava a construir a cada moviment i la volta tornava a
començar a la tira. Amb la placa a la mà, l'amo ho va dir el 7/11: *«no puc pitjar res, i els
botons físics només em mouen per les pestanyes»*. **Aquella trampa ja no hi és** —cada pestanya
és una parada i les fletxes no trien res—, però una pàgina que es torna a construir per un altre
motiu (un botó que la canvia, una dada que arriba) continua perdent el dial, i per això hi ha la
crida.

`ui.focus(control)` diu on ha d'anar el dial:

```lua
local first = nil
local function keep(control)
  if first == nil and control.focusable == true then first = control end
  return control
end
-- ... es construeix la pàgina ...
if first ~= nil then ui.focus(first) end
```

**I el cercle surt al vidre de seguida**, sense esperar el `present` de la pàgina: aquesta
crida es fa just després de construir, i un cercle que no es veu no és cap resposta.

**I la pàgina s'esborra abans de construir-la** (`ui.clear(15)`): un control pinta el seu
rectangle i no més, o sigui que una pàgina de tres controls darrere d'una de sis deixa els
tres de l'abans al vidre.

### Una parada per pestanya: la tira com la del projecte del paper (0.99.0)

**A la tira, el dial no tria: camina.** Cada pestanya és una parada pròpia —el projecte del
paper fa que les pestanyes siguin `Button` i que el navegador les recorri d'una en una
(`tab_strip.h`: *«The tabs ARE `Button`s and the strip hands them to the navigator as
focusable»*)—, i el centre és qui tria la pestanya on és el cercle:

- les **fletxes** entren a la tira, van d'una pestanya a la següent i **surten** per qualsevol
  dels dos caps cap a la pàgina de sota, o sigui que la tira ja no s'empassa cap tecla;
- el **centre** posa `index` a la pestanya del cercle i l'app ho sent a `on_change(self, index)`;
- la **pestanya del cercle es pinta invertida** (la cara pren la tinta, la paraula el paper i
  la línia i el marc van a l'inrevés), que és el mateix senyal que fa servir un botó enfocat;
- `focus_index` diu quina pestanya té el cercle (1 a n, i 0 quan el cercle és en un altre
  control), i una app pot llegir-lo.

**La prova del vidre** és a l'emulador: dues proves noves —una parada per pestanya, i la inversió
llegida píxel a píxel de la superfície— i totes dues es posen vermelles quan se'ls treu la línia
que proven.

### Quants controls hi caben: els que la memòria deixi (0.102.0)

**No hi ha cap número de controls.** La taula de controls la pren el nucli de la **memòria extra** (la PSRAM de la
Paper) quan arrenca, i `ui::canvas_count()` diu quants n'hi ha: mig megabyte de taula són **2 978 controls** de 176
bytes cada un (mesurat al registre, `[I][UI] the table of canvases: 2978 slots of 176 bytes`, i el que sobra de PSRAM
queda per als buffers, que és el que de debò costa (una vista de 540 × 912 són 246 KB). Si el bloc no hi cap, la mida
es parteix per dos fins que hi càpiga, o sigui que una placa amb poca memòria extra en té menys i no es queda sense
pantalla. **`allocate` cau a la RAM del xip quan no hi ha memòria extra**, i això és el que fa que en una placa sense
PSRAM la taula mengi RAM interna abans que cap app corri: és el risc pendent que explica
[`75`](75-pressupost-de-memoria.md) §«La taula de canvas i la RAM interna».

**Un control no té cap sostre de fills**: la llista de fills és enllaçada (el primer i el següent), i un panel de
quaranta files es col·loca sencer. Abans hi havia `kMaxControls = 32` escrit al capçal, i era **tres** coses alhora:
quants controls cabien, quants fills podia tenir un control, i —al pont— quantes parades podia recórrer el dial. Les
tres són ara la mateixa: la memòria.

**I una vista amb buffer tampoc** (`0.174.5`): fins llavors hi havia **vuit** controls amb buffer alhora —la vista de
l'app inclosa—, escrits al capçal com `kMaxCanvases`, i era l'últim número d'aquell fitxer que la memòria no podia
dir. El launcher d'onze apps en demanava onze de 72 × 72 i la vuitena tornava `nil, E_NOMEM`, o sigui que **quatre
caselles de la graella es quedaven sense icona** i la pàgina no en deia res (trobat per l'amo, 10/11). Ara el que no
hi cap ho diu l'assignador, i una app que rep `nil` té el motiu a la segona tornada: `local box, why = ui.canvas(w, h)`
(docs/75, docs/90).

### Què pot contenir què: el control que anida (0.101.0)

**Un control que conté un altre control és un canvas amb fills.** Tots els controls són el mateix objecte —una vista o
una caixa amb un lloc dins del pare— i el dibuix fa el pare i **després** els fills, retallats al marc del pare. Per
això `pare:add(fill)` existeix a tot arreu; el que canvia és **si el pare pinta el seu contingut ell mateix**.

| Control | Fill | Per què |
|---|---|---|
| `ui.root()` / `ui.canvas()` | **sí** | la vista de l'app |
| `panel` | **sí, i col·loca** | `flow` vertical/horitzontal/overlay, `padding`, `gap` i `weight`: és l'únic que reparteix l'espai |
| `button` | **sí** | el fill es compon sobre la cara; **la cara invertida arriba al fill** (vegeu abaix) |
| `checkbox` | **sí** | la caixa a l'esquerra; de `cb:box()` cap a la dreta el marc és de l'app |
| `label` | **no** | és una línia de text i el pintor omple tot el marc → `E_INVAL` |
| `dots` | **no** | són les boles de la pàgina i no hi ha cap lloc per a res més → `E_INVAL` |
| `tabs`, `list`, `menu`, `menu_bar`, `month`, `icon_grid`, `notification_bar`, `selector`, `dialog` | **encara no** | els seus elements són **camps** (paraules i números): no hi ha ni un lloc per fill per element ni geometria per element, i el pintor dibuixa ell mateix el mot |
| `row`, `progress`, `slider`, `sparkline`, `pie`, `textbox`, `textbox_multiline` | tècnicament sí | però el pare pinta tot el marc i el fill hi queda a sobre: no és un contenidor útil |

**La cara invertida arriba als fills.** Un botó amb el cercle (o amb el dit a sobre) pinta la cara de tinta; el que
porta a dins es dibuixa **sobre tinta** i intercanvia els seus dos tons, o sigui que una etiqueta dins d'un botó
enfocat és **paper sobre tinta** i no un forat blanc dins d'una caixa negra. Ho fan `label`, `row`, `checkbox`,
`sparkline` i `pie`; un `panel` no pinta res i només passa el testimoni. **No ho fa una imatge**, perquè una imatge
no és un control sinó una crida (`ui.image`).

**I el control que es gira ho diu ell mateix** (0.106.0): el que passa als fills no és una llista de noms dins del
dibuix, sinó una propietat del control (`button`, `checkbox`, `row` i les entrades de llista i de graella la porten).
Un control que pinta la seva pròpia cara de tinta quan té el cercle, el dit o la marca **dirigeix els seus fills a
l'altra cara**; si ja està sobre tinta, l'intercanvi el torna a paper, que és el mateix d'un nivell més avall. Abans
d'aquesta regla la llista era dins del dibuix i només tenia el botó i la casella, i per això una **entrada marcada
deixava les seves etiquetes sobre paper**: un pegat blanc damunt la tinta de la fila.

```lua
local b = ui.button(400, 80)
local words = ui.label(360, 44)      -- el pintor del botó no el toca: el dibuixa l'etiqueta
words.text = 'inside'
b:add(words)
```

La inversió **no és un estat del sistema**: viatja com a argument del dibuix (`draw(self, depth, inverted)`), perquè
un dibuix que falla i surt de l'intèrpret deixaria un flag encès i la resta de la pàgina sortiria girada (docs/92 §15).

**I un fill que no sap dibuixar-se sol no es gira** (0.178.0): un `ui.canvas` sense `on_draw` compon els mateixos píxels
que té, i per això la icona del llançador quedava com un **quadrat blanc** damunt del bloc negre de la cel·la. El que ho
arregla és el que el projecte vell feia amb una sola crida sobre la cel·la triada (`invertRoundRect`): el canvas de la
imatge té un `on_draw` que la torna a pintar i, quan el segon argument diu que és sobre tinta, **gira la caixa sencera**
amb `cv:invert(0, 0, w, h)` — el paper de la icona es torna el negre de la cel·la i els grisos del dibuix es queden,
que és el que fa que la icona sembli no tenir caixa.

### Els estats d'un control: seleccionat, premut i deshabilitat (0.100.0)

Un control no és una foto: té estats, i cada estat es veu al vidre i diu què pot passar si el prems.

| Estat | Camp | Com es veu | Què respon |
|---|---|---|---|
| **Normal** | — | el dibuix de sempre | tot |
| **Seleccionat** (té el dial) | `focused` | invertit o amb anella, segons el control, **i només mentre es fan servir les tecles** (0.179.7) | el centre, i les fletxes si té valor |
| **Premut** | `pressed` | la cara baixa quatre píxels i la vora es canvia | res mentre dura: és el moment |
| **Deshabilitat** | `disabled` | la seva tinta baixa al `dim` del control (8 per defecte) | **res**: ni el dial, ni el dit, ni el teclat |

**`disabled = true` és l'estat que diu «ara no».** El control continua a la pàgina i es dibuixa —una persona ha de
poder llegir què no està disponible—, però **la tinta cau al `dim` del propi control** (la mateixa que ja fa servir
una paraula secundària) i el paper no es toca: el fons, i la pista d'un slider, queden com eren. Es pinta **sense
l'anella**, perquè no pot tenir el focus.

**I no rep res**: `ui_target_is_focusable` el refusa, i és la pregunta que fan els tres camins d'entrada —la volta del
dial, el dit i el teclat (que només s'obre en un quadre que la volta li ha donat)—. O sigui que un control apagat no
es pot enfocar, ni prémer amb el dit, ni escriure-hi.

**L'estat baixa als fills**, com a qualsevol sistema: apagar un botó apaga l'etiqueta que porta a dins, i tornar-lo a
encendre la torna. I **baixa igual si el fill arriba després** (0.106.0): un control afegit a un pare apagat **neix
apagat**, perquè sinó el pare es va apagar quan el fill encara no hi era i la pàgina quedava amb un forat per on el
dial hi entrava.

**Un fill que l'app ha apagat pel seu compte no el torna a encendre el pare** (0.106.0): el sistema guarda de qui ve
cada `disabled` (`__off_father`), o sigui que tornar a encendre un panell retorna els fills que ell va apagar i deixa
apagats els que l'app va decidir. Abans, encendre el pare encenia tot el que hi havia a dins i es menjava la decisió de
l'app.

```lua
local save = ui.button(300, kButton)
save.disabled = true          -- es dibuixa fluix i no respon a res
-- ... arriba la connexió ...
save.disabled = false         -- torna el dibuix de sempre i la volta hi pot caure
```

**Escriure `disabled` repinta el control** (i els seus fills), i si el control tenia el dial, el torna: deixar
l'anella sobre un control que cap tecla no pot tocar és una pàgina que menteix sobre on caurà una pulsació.

### Qui té el focus: `focusable` és de cada control (0.106.0)

**Només un control té el focus, i els fills no el tenen: el dibuixen.** Un botó amb una etiqueta a dins és **una
parada** del dial —el botó— i l'etiqueta no n'és cap; el que passa és que la cara invertida del botó arriba al fill i
la paraula surt en paper. La confusió de «qui té el focus aquí» es desfà amb una pregunta sola: **qui és al
recorregut**, i això ho diu `focusable`, que és un camp de **tots** els controls.

| Control | `focusable` per defecte | Per què |
|---|---|---|
| `button`, `checkbox`, `slider`, entrades de llista i de graella | `true` | són el que una persona prem o mou |
| `tabs`, `list`, `icon_grid`, `menu`, `menu_bar`, `selector`, `notification_bar`, `month`, `dialog`, `textbox`, `textbox_multiline` | `true` | tenen un valor o una pàgina que les fletxes mouen: el dial hi ha de poder caure |
| `panel`, `row`, `progress`, `sparkline`, `pie`, `dots`, `label`, `ui.canvas`, `ui.control` | `false` | són un dibuix o una cel·la: el control que interessa és el de dins, o cap |

- **`control.focusable = true`** fica qualsevol control al recorregut —una etiqueta, una cel·la d'un `panel`,
  `ui.control(w, h)`—, i **`= false`** en treu un que hi era de sèrie. És el que fa que un botó físic sàpiga a qui pot
  donar el focus: la volta, el dit i el centre pregunten el mateix (`ui_target_is_focusable`), i un control que diu
  `false` no el rep mai de cap de les tres maneres.
- **`focusable = false` no vol dir apagat**: el control es pinta igual i l'app el pot prémer pel seu compte; el que no
  fa és entrar al recorregut del sistema.
- **El valor és `true` o `false` i res més**: un `3` és un error (`a control's focusable is true or false`), perquè és
  l'únic camp que decideix si una persona pot arribar a un control.
- **Un control deshabilitat no és enfocable encara que digui `focusable = true`**: el `disabled` mana. I si perd el
  dial en el moment que s'apaga, el torna, i el primer control enfocable que aparegui el recull.
- **El focus el col·loca l'app amb `ui.focus(control)`** quan una pàgina s'ha tornat a construir, i no fa res si aquell
  control no és al recorregut.

```lua
local b = ui.button(400, 80)
b.focusable = false           -- es pinta, però la volta no s'hi atura
local w = ui.label(400, 44)
w.focusable = true            -- una etiqueta que sí: el centre la prem
```

**Les entrades d'una barra de menú ja tenien el seu `enabled`** des de 0.68.0, amb el mateix dibuix apagat i salts a
la volta; ara és el mateix to i la mateixa regla. **Les opcions d'un menú de targeta no**: allà el que no es pot fer
s'amaga, perquè una targeta és la llista del que es pot fer **ara** (docs/92 §14).

### El selector es veu quan es fan servir les tecles (0.179.7)

**L'anell no és una decoració: diu on caurà la pulsació del centre.** Una persona que llegeix la pàgina amb el dit no
necessita aquest avís —el dit ja diu on és— i l'anell és tinta que s'ha de netejar cada vegada que es toca una altra
cosa. L'amo ho va demanar així: *«Si s'usa el dit el selector del focus ha d'amagar-se. Si es tornen a usar els botons
físics ha de tornar. Per defecte el selector de focus ha d'estar desactivat fins que s'usen els botons»*.

- **De sortida no hi és**: una pàgina que s'acaba d'obrir ensenya els controls amb el dibuix de sempre, sense cap
  anell, encara que el dial ja sigui en un d'ells.
- **La primera tecla el fa sortir** on el dial ja era, i la mateixa tecla fa el que havia de fer: un botó que només diu
  «entra» pinta l'anell abans d'entrar, o sigui que la persona veu on era abans de veure-ho canviar.
- **El primer dit el torna a amagar**, i la pàgina es repinta al mateix torn: el control que el tenia queda sense anell
  encara que no hagi canviat res més de la pàgina.
- **I no es perd res**: `focused` continua volent dir «el dial és aquí» i l'app continua rebent `on_focus` mentre
  l'anell no es veu, i la tecla següent camina **des d'on era** i no des del principi de la pàgina. Un anell amagat no
  és un dial perdut.
- **Cada app comença sense anell**: l'estat viu amb la màquina de l'app, o sigui que obrir-ne una altra és tornar al
  defecte —amagat— i el primer botó d'aquella app el fa sortir. És el que vol dir «per defecte»: el que encara no ha
  vist cap tecla.

### La tecla es pinta sola: el sistema és qui la rep (0.98.0)

**`ui_button_event` pren la tecla abans que l'app en sàpiga res.** La volta del dial i la
baixada d'un botó són canvis de l'arbre, i l'app només pinta quan demana un `ui.present`: una
app que espera dins d'un `sys.delay` no en demana cap, o sigui que el cercle es movia i el
vidre no deia res. Amb la placa a la mà, l'amo ho va llegir com el que és: *«l'app Showcase no
respon a l'usuari»*. Mesurat a l'emulador, la volta ja hi era —tres parades a la pàgina i el centre
rebut— i el vidre no es movia ni un píxel.

Per això **el sistema pinta el que ell ha canviat**, i ho fa pel mateix camí que l'app: dibuixa
el que deu un dibuix, compon l'arbre i demana el pintat. Com que només viatgen les regions que
han canviat, un cercle que es mou costa la caixa del seu control i no la pàgina. És el
`feedback()` del projecte del paper (`src/ui/button.cpp`: *«the pressed state is immediate
feedback: it is what the user is waiting for»*), i passa a quatre llocs: quan el dial es mou,
quan `ui.focus` col·loca el cercle, a la baixada i a la pujada d'un botó, i **quan una tecla mou
un valor** (0.179.11): el cursor d'una graella, la pàgina d'una llista, el número d'un lliscador o
el dia d'un calendari deien que sí a la tecla i el vidre no es movia fins que l'app presentava pel
seu compte, que és una pàgina que sembla morta.

**Què fa una pulsació, en canvi, és de l'app**: el sistema només ensenya que el control ha
rebut la tecla. Una pàgina els controls de la qual no tenen cap `on_click` respon al dial i no
di res quan es prem, i això és una app muda i no un sistema que no respon. L'app de mostra ho
ensenya: cada control que dona al dial escriu què s'ha premut a la línia de baix.

### La pàgina és un anell, i el que no es veu no és una parada (0.156.1)

El recorregut és **circular sobre els controls de la pàgina**: de l'últim de la pàgina es passa al
primer i del primer a l'últim, i per això no hi ha cap tecla que deixi la persona enlloc —ni al
principi ni al final de la pàgina, que és el que faria una volta que s'aturés als extrems—.
**La llista de parades és la de la pàgina i no la de l'arbre**: un control el marc del qual cau
fora del pare no hi és, i per això el cercle no pot marxar a una cosa que no es veu. **L'excepció és
la cel·la d'una graella** (0.157.0): les cel·les d'una `ui.icon_grid` són parades encara que siguin
d'una altra pàgina del control, perquè **la pàgina ha de poder seguir el cercle** —la cel·la on cau
el dial la col·loca la graella quan el seu `index` es mou—. Sense això, una pàgina amb més cel·les que
les que hi caben no es podia recórrer mai més enllà de la primera.

**I el final d'una pàgina no és el final del camí** (0.179.4): les files d'una llista de les altres
pàgines també són fora del marc, però **una passa més enllà de l'última fila visible gira la pàgina** i el
cercle va a la primera de la nova —i una passa enrere de la primera gira a la pàgina anterior amb el
cercle a l'última fila seva—. És la mateixa promesa de la graella dita per a les files, i és el que fa
que una llista de més files de les que hi caben es pugui recórrer amb les tecles i no només amb el dit. A
l'última pàgina la tecla torna al recorregut i surt de la llista, que és l'anell de la pàgina i no un
cul-de-sac (docs/90).

**Una graella amb les cel·les de l'app a dins és una caixa i no una parada** (0.156.1, i és un
arranjament demanat amb les paraules de l'amo: *«sembla que et pots sortir per l'esquerra i per la
dreta amb el focus»*). Les parades són les cel·les que porta, i la graella com a parada pròpia era
un fantasma on el cercle podia caure —el cercle era tota la caixa de la graella, que el control no
pinta, o sigui que semblava que el focus hagués marxat de la pàgina— i des d'allí la volta seguia
l'ordre de l'arbre i no el de la pàgina. Una graella amb els `items` de la pròpia control **sí** que
és una parada: allà les cel·les són del control i `index` les camina.

**Les pàgines d'una graella són de la graella, i el seu peu és el paginador** (0.157.0, i el paginador des de la
0.175.0): `ui.icon_grid` deixa fora del marc les cel·les de les altres pàgines i **pinta el peu amb el paginador del
sistema** —el mateix dibuix de `ui.dots` i del peu d'una llista, els punts enfilats en una vareta amb la pàgina que es
llegeix més gran—, o sigui que **una app no ha de fer cap paginador**: la graella compta les seves cel·les, sap a quina
pàgina és cadascuna i la resta és seva. El que abans era una banda de dos botons i un número escrits per l'app (0.154.1)
l'amo el va fer treure: *«usa sempre els UIControls que hi hagin abans de fer-ne un de nou»* (8/11). Fins a la 0.175.0 el peu eren les paraules `N / M` i dues marques que volien semblar
fletxes —un tercer paginador dibuixat a mà dins d'aquest fitxer— i l'amo les va fer fora: *«que es 1 / 2 que hi ha sota el
grid? usa el paginador Dots … no inventis controls o dibuixs controls falsos»*.

**I la pàgina es passa amb el dit, d'esquerra a dreta o de dreta a esquerra** (0.175.0): un dit que camina de costat
sobre la graella —quaranta píxels o més, el doble del llindar d'un toc— la passa a la pàgina següent si va cap a
l'esquerra i a l'anterior si va cap a la dreta, i **el cercle va a la primera cel·la de la pàgina nova**, que és el que
fa que la pàgina el segueixi. El gest és d'**una pàgina per contacte** i no és mai una premuda: la cel·la on s'aixeca el
dit no es tria. Una graella que és a l'última pàgina no fa res si el dit hi va cap a l'esquerra —no dona la volta—, i una
app no ha de registrar cap gest: és el control i prou (docs/30, docs/90).

**I el cursor l'escriu el sistema** (0.179.4): la pàgina d'una graella és la del seu `index`, i fins a la 0.179.3 cada
llançador havia d'escriure aquell número a l'`on_focus` de **cada** cel·la —una línia per cel·la, i l'app que se
n'oblida té una pàgina que no gira mai—. El cercle és del sistema i el focus arriba en un sol lloc, o sigui que `index`
es posa allà, abans que es pinti el cercle: una cel·la d'una altra pàgina que pren el cercle és una pàgina que la
graella torna a col·locar, i el seu peu diu on ets sense preguntar-ho a l'app. Una graella amb els `items` del propi
control no té cel·les a qui seguir: allà `index` és el cursor i el mouen les fletxes (docs/30, docs/90).

### `ui.list` amb entrades: files que són controls (0.104.0)

**Una llista de dades és el camí curt i una llista d'entrades és el complet.** Fins a 0.103.0 una llista només podia
ensenyar el que cap en tres camps —`title`, `detail`, `subtitle`—; ara la fila pot ser **un control de l'app**:

```lua
local list = ui.list(420, 180)
list.row_height = 56
local entry = ui.list_entry(420, 56)     -- la fila: paper, separador, marca i focus
local words = ui.label(300, 44)          -- el que hi hagi dins és de l'app
words.text = 'row 1'
words:place(12, 6)
entry:add(words)
list:add(entry)                          -- i la llista la col·loca
```

- **`ui.list_entry(w, h)`** és la fila: pinta el paper —o la tinta quan és la `marked` o la que té el cercle— i la
  **línia del separador**, i **no pinta cap paraula**. El contingut és el que l'app hi posa: una etiqueta, una fila de
  dues cel·les (`ui.panel(…, 'horizontal')`), una caixa amb una imatge i un nom.
- **La llista col·loca les entrades de la pàgina on és**: `row_height` mana, i el peu de les pàgines es reserva sol. Les
  entrades de les altres pàgines es posen **fora del marc**, on el retall no les compon i el dial no hi arriba.
- **Les fletxes giren les pàgines** igual que amb dades, i l'app ho sent a `on_page(self, page)`. El nombre de files és
  el de fills quan no hi ha `items`.
- **I el dit també, i la pàgina és la de la llista i no la de l'app** (0.179.4): un gest de costat passa la pàgina i una
  passa més enllà de l'última fila visible també, amb el cercle posat a la primera fila de la pàgina nova. **L'app no ha
  de llegir cap gest ni escriure cap pàgina**: fins a la 0.179.3 l'`about` tenia un `touch.gesture()` i un `kSlidePx`
  seus per fer girar aquesta llista, i eren un paginador a mig fer dins d'una app (docs/30, docs/90).
- **`items` i les entrades conviuen**: si la llista té fills, es col·loquen ells i el control només pinta el fons i el
  peu; si no en té, es pinten les files de dades com sempre. Una llista no s'ha de migrar per començar a fer servir
  entrades.
- **Cada entrada és una parada del dial** (és un fill, i el recorregut baixa per l'arbre), o sigui que una llista de
  sis files té sis parades a més de la pròpia llista (que és la que gira les pàgines).

### `ui.icon_grid` amb entrades: cel·les que són controls (0.105.0)

**La graella és una pàgina amb una reixa, i les cel·les eren dades.** Fins a 0.104.0 una cel·la només podia ser una
paraula curta (`mark`) i una de llarga (`label`); ara la cel·la pot ser **un control de l'app**, i és el que demana un
launcher de debò: una imatge amb el seu nom, una caixa de dues cel·les, una miniatura:

```lua
local grid = ui.icon_grid(500, 400)
grid.columns = 4
grid.scale = 2
local cell = ui.grid_entry(100, 100)      -- la cel·la: paper i focus
local box = ui.label(80, 40)              -- el que hi hagi dins és de l'app
box.text = 'A'
box:place(10, 10)
cell:add(box)
grid:add(cell)                            -- i la graella la col·loca
```

- **`ui.grid_entry(w, h)`** és la cel·la: el mateix control que `ui.list_entry` —paper, tinta, `marked` i el cercle— i
  **sense la línia del separador**, perquè una reixa no té files separades. **No pinta cap paraula**: el contingut és
  el que l'app hi posa.
- **Qui diu on va cada cel·la és el nucli**, amb la mateixa aritmètica que el pintat i el dit: la graella en dona la
  caixa a l'app (`icon_grid_cell`, per dins del pont) i el pont hi posa el control amb `canvas_resize` i
  `canvas_place`. Una cel·la d'una pàgina on la graella no és **no té caixa**, i el pont la posa fora del marc, allà
  on el retall no la compon i el dial no hi arriba.
- **`items` i les entrades conviuen**, com a la llista: si la graella té fills, la cel·la és el fill i el control
  només pinta **el paper de la pàgina i el peu**; si no en té, es pinten les cel·les de dades com sempre. **El nombre
  de cel·les d'una graella d'entrades és el de fills**, i amb ell es compten les pàgines que diu el peu.
- **Les fletxes caminen les cel·les i la pàgina segueix el cursor**, igual que amb dades: no hi ha res a fer a l'app —ni
  tan sols posar `index`, que el sistema escriu on arriba el cercle (0.179.4)— a part de sentir-ho a `on_page(self, page)`
  si li convé.
- **Cada cel·la és una parada del dial** pel fet de ser un fill, com les entrades de la llista.

### `ui.list`: files en pàgines, i mai scroll (0.71.0)
**A la tinta no es fa scroll**: un scroll és un centenar de pintats petits i una pàgina és un de sol. Per això una
llista **pagina**, i el peu diu en quina pàgina ets.

- **`items` és una taula de taules** (`{title=..., detail=..., subtitle=...}`) o **de cadenes**, que són el títol.
  Les files es pinten amb el mateix pintor que `ui.row` —un sol pintor, o la llista i la fila divergiran— i la
  fila `marked` s'inverteix.
- **Quantes files hi caben i quantes pàgines surten és una sola aritmètica**, i la pregunta la contesta el nucli: el
  peu només existeix si hi ha més d'una pàgina i, quan hi és, es queda la seva banda i les files es compten **de nou**
  en el que queda. Una llista que digués «2 / 4» sobre les files de la cinquena pàgina seria una llista que menteix.
- **El peu és el paginador del sistema i va sota l'última fila** (0.174.2, l'amo: *«a la part d'abaix has posat un
  pager? perqué ocupa tant espai i es veu tant petit? aquest paginador es del sistema?»*). Sí que ho és, i ara ho és
  del tot: el peu d'una llista i `ui.dots` són **un sol dibuix** —les boles del projecte del paper, el pal que les
  enfila, la bola de la pàgina que es llegeix plena i més gran, les altres d'anella i les tres boles que s'encongiran
  quan les pàgines no hi caben— **amb la mateixa bola del disseny a tots dos** (0.176.0: abans la llista l'escalava a la
  banda i el mateix control sortia de 20 píxels en una llista de files curtes, de 24 sota una graella i de 25 dins d'un
  `ui.dots`), i no quatre punts de quatre píxels que eren una segona còpia de la mateixa idea i la petita. **La banda ja
  no depèn de la fila**: són els 37 de la caixa del disseny més les 4 de la línia que tanca les files, i el peu es pinta
  **on s'acaba l'última fila** i no al fons de la caixa: una llista amb més lloc que files deixava l'aire **entremig**, i
  això es llegia com un peu que ocupa mitja pàgina. El paper que sobra és sota el peu, on s'acaba una pàgina. Quan la
  caixa és tan estreta que la tira no hi cap, el control pinta el número (`2 / 4`) i prou (docs/30, docs/92).
- **`marked` és una fila de tota la llista** i el control en pinta una pàgina: una fila d'una altra pàgina no és la
  triada d'aquesta, i el control ho sap (zero).
- **Les fletxes del dial canvien la pàgina** mentre la llista té el focus i l'app ho sap per `on_page(self, page)`.
  El peu només ensenya el galó del costat on hi ha alguna cosa, i al final del recorregut la tecla torna al dial.
- **I el dit la canvia igual** (0.179.4): un gest de quaranta píxels o més cap a l'esquerra passa a la pàgina següent i
  cap a la dreta a l'anterior, **una pàgina per contacte i mai una premuda** —la fila on s'aixeca el dit no es tria—, i
  una llista que és a l'última pàgina no fa res si el dit hi va cap a l'esquerra: no dona la volta. És el mateix gest de
  la graella i el mateix avís per `on_page`, o sigui que **una app no ha de llegir cap gest per girar una pàgina**: el
  paginador és del control i el control el gira ell (docs/30, docs/90).
- **La pàgina no es queda mai enrere del cercle** (0.179.4): les files de les altres pàgines són fora del marc i no són
  parades, o sigui que una passa més enllà de l'última fila visible **és la pàgina** —i el cercle va a la primera de la
  nova—, i una passa enrere de la primera gira a l'anterior amb el cercle a l'última. A l'última pàgina la tecla torna
  al dial i surt de la llista.
- **No hi ha cap `visible` i cap control amagat**: la llista no té fills, sinó una taula de dades. Una llista feta de
  controls `ui.row` és possible, però llavors les files que no toquen es dibuixarien igualment i el que s'hauria de
  fer és una altra cosa (docs/90).
- **Una llista que ha d'omplir la pàgina es posa amb `weight = 1`** (0.174.3): el panel reparteix el lloc que sobra i la
  caixa de la llista arriba al marge de baix, o sigui que **hi caben més files i es passa de pàgina menys sovint**. Una
  altura mesurada a mà —`view.height` menys les línies del capçal i unes files de marge— és una endevinalla que deixa
  paper a sota, i una llista amb més lloc que files deixa aquell paper **dins** de la seva caixa: el que el panell dona
  amb `weight` és l'única altura que no s'equivoca (docs/30, docs/90).

```lua
local l = ui.list(view.width - 32, 360)
l:place(16, 120)
l.row_height = 56
l.separator = true
l.items = {
  { title = 'Res',  detail = '1 033 B', subtitle = 'the sample files' },
  { title = 'Apps', detail = '48',      subtitle = 'the apps of this build' },
  { title = 'Data', detail = '4 096 B', subtitle = 'what the apps wrote' },
}
l.marked = 2
l.on_page = function(self, page) console.print('page ' .. page) end
ui.present()
```

### `ui.textbox` i el teclat del sistema (0.72.0)

**El cursor és la meitat important d'una caixa de text**, i aquí és un **bloc tan ample com la lletra que té a
sota**, pintat amb la tinta i amb aquella lletra en paper —un bloc ple taparia la lletra que es va a escriure a
sobre, i un contorn seria una caixa dibuixada sobre una altra caixa—. **Al final del text, on no hi ha cap lletra, el
bloc fa l'amplada d'una `M`** i no la barra de tres píxels de les primeres caixes (0.92.0, docs/90).

- **El text és una finestra**: si la línia no hi cap, cauen les lletres del començament fins que el cursor queda
  al vidre. Sense això, qui escriu al final d'una paraula llarga escriu en un lloc que no es veu (docs/90).
- **El parpelleig és del sistema** (0.92.0): mentre la caixa és la del teclat, el nucli gira `box.caret` cada
  sis-cents mil·lisegons i torna a pintar la caixa amb un `du`; l'app no ha de portar cap rellotge ni cridar `present`
  per fer parpellejar el cursor. Escriure una tecla torna el bloc a visible i reinicia el comptador.
- **`mask`** dibuixa un puntet per lletra: és el mateix requadre amb els punts al lloc de les lletres, i per tant el
  cursor cau on cauria i l'amplada del que s'ha escrit no es filtra.
- **`cursor` és quants bytes hi ha abans del cursor**, no una lletra: una lletra pot fer dos bytes.
- **`keyboard` és el que la caixa vol del teclat** (0.91.0): `'alpha'` per defecte, `'numeric'` per a dígits, i
  `false` per a una caixa que no en vol —una pàgina de només lectura, un full que només es desplaça—. El sistema el
  llegeix quan la caixa pren el focus, i `kb:show('numeric')` també l'escriu a la caixa.

**El teclat no és un control, és del sistema, i des de 0.91.0 segueix el focus.** És una **banda** que el nucli
pinta a baix de la vista, sobre la pàgina ja composta, i que **pren el dit i el dial abans que els controls de la
pàgina**. **Quan una caixa de text que pren tecles rep el focus, la banda surt i la caixa passa a ser el target; quan
el focus se'n va a un botó, a una targeta o a la pàgina buida, la banda es tanca** (docs/30, docs/90).

**Des de 0.93.0 el teclat és una classe amb un pare comú.** `Keyboard` té la banda, el dit, el dial, el bloc de la
tecla triada i la cara; cada fill (`QwertyKeyboard`, `AzertyKeyboard`) només diu el seu id i les seves files.
L'app no canvia res: `ui.keyboard()` i les mateixes crides parlen amb el fill que el sistema tingui triat. Es tria a
la consola amb **`cfg keyboard qwerty`** o **`cfg keyboard azerty`** —`cfg keyboard` en dona la llista—, la tria es desa
a l'NVS i l'arrencada la torna a posar. Un teclat nou és un fill més i una fila a la llista, no una altra banda
(docs/70, docs/90).

| Crida | Què fa |
|---|---|
| `kb.target = box` | La caixa on escriu. El sistema la mou a la caixa que pren el focus; l'app la pot escriure abans d'un `kb:show` |
| `kb:show('alpha')` · `kb:show('numeric')` | La banda surt per al target, amb el joc que es demana. **Exigeix una caixa de text viva que prengui tecles**: sense target, amb un objecte que no és una caixa, o amb `keyboard = false`, torna `nil, E_INVAL` |
| `kb:hide()` | La banda marxa (i marxa sola quan l'app s'acaba). **No esborra el target**: una caixa que encara pren tecles la torna a obrir quan el dit hi torna; perquè no ho faci, `box.keyboard = false` |
| `kb:height()` | **La banda que pren** —256 al Paper des de 0.107.0, quatre files de 64—, que des de 0.84.0 ja no cal restar de res: és el que es pregunta per pintar una barra pròpia just a sobre |
| `kb:visible()` | Si és al vidre |

**La vista de l'app és el que queda sobre la banda** (0.84.0). `ui.caps().height` i `ui.height()` diuen **656 i no 912**
mentre el teclat és al vidre, i l'app rep **`on_resize(width, height)`** quan la banda surt i quan marxa —el mateix hook
d'una vista que canvia de mida o de gir—, o sigui que una pàgina que es fa a partir de `ui.caps()` ja queda bé i no ha de
restar res. El llenç de sota no es torna a demanar (no costa ni un byte: la banda es pinta a sobre dels seus últims 256
píxels), i `ui.canvas().height` continua dient l'alçada del llenç per a qui necessiti la mida física.

```lua
local kb = ui.keyboard()
kb.target = box             -- el sistema també ho fa quan el dit hi cau
kb:show('alpha')            -- la banda primer (o el dit, des de 0.91.0)
local view = ui.caps()      -- i la vista ja és el que queda: 656
local page = ui.panel(view.width, view.height, 'vertical')

function on_resize(width, height)   -- la banda ha vingut o se n'ha anat
  page:resize(width, height)
  ui.present()
end
```

- **L'app no mana cap tecla**: cada tecla arriba a la caixa que ha nomenat, la caixa escriu el camp i tot seguit
  avisa amb `on_change(self, text)`. El **13** no és cap lletra: és el `OK`, i la caixa diu `on_enter(self)` i no
  toca el text.
- **El focus de la pàgina i el del teclat són el mateix quan hi ha un dit** (0.91.0): un dit sobre una caixa
  l'enfoca (`focused`) i li dona el teclat; un dit sobre un botó enfoca el botó i tanca la banda; un dit fora de tot
  control treu el focus, tanca la banda i oblida el target. **Les caixes de text són objectiu del dit i no del dial**:
  el dial no les recorre, i la banda que ja és al vidre és la que pren les tecles. En una placa sense dit, l'app obre
  la banda amb `kb:show()`.
- **Una banda no s'obre sense caixa** (0.91.0): `kb:show` sense target, o amb una caixa que diu
  `keyboard = false`, és `E_INVAL`; una caixa amb `keyboard = false` tampoc no obre la banda quan el dit hi cau. El
  que sí que fa el dit sobre una caixa de només lectura és posar-hi el cursor i deixar-lo desplaçar si té `on_swipe`.
- **Les fletxes del dial caminen les tecles** i el centre prem la que hi ha: una placa de tres tecles també pot
  escriure. El recorregut va **per les tecles que es veuen**, en l'ordre en què es pinten, i no per una graella de
  caselles buides (0.83.0).
- **La tecla on ets s'inverteix** i la resta són lletres i prou: des de 0.83.0 el teclat no dibuixa cap botó, i el
  que diu «aquesta és la que prems» és el bloc de tinta.
- **Les lletres són una tipografia de debò** (0.85.0): `DejaVu Sans Bold` de `/fonts`, dibuixada amb
  `font::load_ttf`, i la cara 5 × 7 del sistema només quan el disc no porta `/fonts`. El joc alfa és un QWERTY
  esglaonat —la fila de la `a` va mitja tecla més a la dreta que la de la `q`— amb la **barra d'espai** al seu lloc,
  la tecla de **majúscules** (`MAY`) a l'esquerra de la `z` i la de **retrocés** a la dreta de la `m`. Les tecles
  que no són lletres es dibuixen amb el signe i no amb la paraula: el **pentàgon amb una creu a dins** per esborrar
  (0.107.0), ↵ per l'enter, i la paraula `space` per la barra.
- **Els dos símbols dibuixats són una sola peça i estan centrats** (0.109.0): el traç d'una diagonal es dibuixa
  **centrat a la seva línia** —abans cada pas posava el bloc del traç a la cantonada, i la creu del retrocés sortia mig
  traç cap a la dreta i tocava el costat del pentàgon—, la creu es mesura des del **cos** del pentàgon i no des de la
  caixa, i la barra de l'enter **arriba al traç que baixa**, amb la punta a l'extrem esquerre; abans la barra s'acabava
  `head` píxels abans del traç i la tecla es veia com dos segments amb un forat. El símbol fa **tres cinquens de
  l'alçada de la tecla** (38 píxels en una tecla de 64): amb dos cinquens, vint-i-cinc, el pentàgon i la creu es toquen.
- **La tecla fa 64 píxels d'alt i la lletra ocupa l'alçada de la tecla** (0.107.0): abans eren 44 i la lletra es
  quedava en dos passos de cos —divuit píxels en una banda de cent setanta-sis—, i l'amo ho va dir tal com es veu al
  vidre: *«el teclat ha de ser més alt i amb tecles i lletres més grans, gairebé no es poden polsar»*. Aquest panell
  llegeix un dit a uns sis píxels per mil·límetre, o sigui que 64 és una mica més d'un centímetre, el més petit que un
  polze prem sense mirar. **La banda no passa mai de dos cinquens de la vista** i, en un vidre petit, s'encongeix ella
  (mínim 32); el sostre de la lletra és **5 passos**, que és el que cap al bloc de noranta-sis píxels del glif
  antialiasejat.
- **Cada lletra es pinta a doble mida i es rebaixa a quatre nivells de tinta** (0.85.0): la cara dona corbes i el
  vidre té setze nivells, o sigui que la diagonal d'una `x` són quatre passos i no una escala de píxels. És el que fa
  que el teclat no sembli una graella. El fitxer són **8,5 KB** de la memòria extra i es té mentre la banda és al
  vidre.
- **`MAY` és un bloqueig i no un shift** (0.85.0): premut, totes les lletres es pinten en majúscula i la tecla queda
  girada; **es queda així fins que es torna a prémer**. Una lletra no el desactiva —un shift d'una lletra és el d'un
  telèfon, i una pàgina que s'escriu amb el dial vol el mode on la persona l'ha posat—.
- **Prémer una lletra no repinta la banda** (0.85.0): el que canvia és la caixa, i el dibuix del teclat només es
  torna a fer quan canvia el bloqueig, l'alfabet, o quan la pàgina passa per sobre de la banda (`ui.clear`, un
  rectangle o un píxel): la banda viu al llenç i el sistema la torna a posar al següent `present`.
- **La banda fa quatre files en els dos jocs** (0.83.0): un canvi de joc canvia les tecles i no l'alçada, o la
  pàgina que s'ha col·locat a sobre es mouria en prémer el `123`.
- **La banda es pinta amb `gl16` i no amb `du`**: un joc nou posa tinta on hi havia les lletres del vell, i una
  actualització ràpida deixa les velles a mig esborrar —dos teclats a la mateixa banda, que és el que es va veure a
  la banqueta en canviar de joc (0.83.0, docs/50, docs/90).
- **I només s'envia al vidre el que ha canviat** (0.83.1): la banda es pinta al llenç a cada `present` perquè és la
  capa de sobre, però una lletra, un espai o un retrocés **no en canvien cap píxel** i no en marquen cap. El bloc de la
  tecla que es mou són dues cel·les, i el canvi de joc o de caixa és la banda sencera. Sense això el teclat
  **parpellejava a cada sis-cents mil·lisegons**, que era el que l'app que hi escrivia trigava a fer parpellejar el
  cursor de la seva caixa; des de 0.92.0 el parpelleig és del sistema (docs/50, docs/90).

- **Una pàgina que es tecleja ha de pintar el que ha teclejat, i amb una qualitat que neteja** (0.84.1). Un `du`
  deixa **cinc setzens** del que hi havia —és la gràcia del mode ràpid i el que fa que la pàgina es vegi com una
  pàgina de tinta— i en una caixa de text això són les lletres de la tecla anterior a sota de les noves, que és el que
  es va veure en una pàgina que es teclejava amb cinc tecles. El que fa una app que escriu és `ui.present('gl16')` al seu `on_change`
  —una neteja amb un parpelleig— i deixar el `du` per al cursor, que és un bloc i el neteja la tecla següent.
  **Una app que no presenta no pinta res**: la caixa guarda el text i el vidre no se n'assabenta fins al pròxim
  `present`, o sigui que un teclat que «no funciona» pot ser una pàgina que no es torna a pintar mai.

```lua
local box = ui.textbox(view.width - 32, 56)
box.scale = 3
box.cursor = 0
box.on_change = function(self, text)
  console.print('now "' .. text .. '"')
  ui.present('gl16')                -- el text va al vidre, i net
end

local kb = ui.keyboard()
kb.target = box
kb:show('alpha')
view = ui.caps()                  -- i el que la pàgina té és el que queda: 656 (0.84.0)
ui.present()
```

Una caixa, el teclat, el cursor que parpelleja i el que diu el `on_enter` són la pàgina sencera.

### `ui.dialog`: una targeta i dues portes (0.73.0)

**Un diàleg és una targeta opaca sobre la pàgina** —el marc de tres píxels és el que diu que és a sobre i no una
part de la pàgina— amb un títol, un **missatge que s'ajusta en línies** i dues portes.

- **El missatge no es talla amb `...`**: una targeta que diu mitja pregunta és una targeta que ningú no pot
  contestar. Les línies trenquen on hi caben (a l'últim espai que hi entra) i l'espai que queda sobre les portes és
  el que decideix quantes n'hi ha — sis com a molt (docs/90).
- **`emphasis` diu quina porta és la de defecte** i s'inverteix (tinta amb les paraules en paper); **`choice` diu on
  és el dial** i porta l'anell. Dos senyals perquè en aquest vidre no hi ha color per dir «aquesta és la bona».
- **Les fletxes caminen les dues portes** i el centre prem la que hi ha: `on_accept(self)` o `on_cancel(self)`.
- **El dit tria la porta que toca**, no la que tenia l'anell: la geometria de les portes és una sola i la pregunten
  el dibuix i el dit (docs/90).
- **Tancar la targeta és de l'app**, amb la regla del vell: llegir l'acció, tancar i després executar-la —així una
  acció pot reiniciar la placa sense la targeta a sobre (`OLD/PaperOS/src/ui/modal.h:44`).
- Una targeta estreta posa **una sola porta** a tot l'ample; la que es diu és l'`accept`.

```lua
local card = ui.dialog(380, 260)
card:place(80, 200)
card.title = 'Delete?'
card.text = 'This card deletes every note you wrote and there is no way back'
card.note = 'Only yours'
card.accept = 'Delete'
card.cancel = 'Keep'
card.emphasis = 1                       -- el cancel·lar és el de defecte
card.on_accept = function(self) console.print('deleted') end
card.on_cancel = function(self) console.print('kept') end
ui.present()
```

Una targeta i prou és el que cal per provar el diàleg amb el dial i amb el dit: `ui.dialog` torna la porta
que l'usuari ha triat, o `nil` si l'ha tancada.

- **El missatge té la sala que queda dins la targeta i no la de la pàgina** (0.171.0): fins a la 0.170.2
  el compte de línies barrejava la fila del text —mesurada dins la targeta— amb la fila de les portes
  —mesurada dins la pàgina—, i una targeta que no era a la primera fila del vidre es quedava amb **zero
  línies de missatge** i només ensenyava el títol. Es va trobar amb la targeta de `wipe` sobre l'emulador
  (9/11) i ara la sala es mesura dins la targeta, que és el que el dibuix sempre havia volgut dir
  (docs/30, docs/90).
- **Una targeta sobre la pàgina pren el dial, el dit i el centre, i els torna quan se'n va** (0.172.0 i
  0.172.1): mentre un `ui.dialog` és viu, **les úniques parades del recorregut són la targeta i el que hi
  ha dins**, o sigui que una fletxa o el centre no poden caure en un control de la pàgina del darrere i una
  persona no pot contestar sense voler una pregunta que no ha llegit. El control que tenia l'anell el perd
  (i l'app ho sent a `on_focus(self, false)`, 0.173.1) i la targeta el pren; **quan la targeta marxa, l'anell
  torna a aquell control** en el mateix `present` que tanca la targeta, o sigui que una app no ha de cridar
  `ui.focus(card)` ni tornar el focus a mà —l'única cosa que li toca és tancar la targeta quan en rep la
  resposta—. **I un dit que cau fora de la targeta és el seu cancel·lar**: el toc no arriba a la pàgina del
  darrere, i el contacte queda gastat, perquè el dit que ha tancat una pregunta no premi el botó que hi havia
  a sota (0.173.1). Un `ui.menu` i un `ui.month` no són targetes i no tenen aquesta regla (docs/30, docs/90).
- **I la targeta no té píxels propis, o sigui que el forat que deixa s'ha de tornar a pintar** (0.173.2 i
  0.173.3): una targeta sense buffer pinta la seva tinta **dins dels píxels de la pàgina**, i ningú no guarda una
  còpia del que hi havia a sota. Quan l'app la deixa anar, el requadre queda **degut a la pàgina**, i qui el paga
  depèn d'una sola cosa: **si la vista té un `on_draw`**.
  - **Amb `on_draw`, el forat el pinta l'app** (0.173.3): el `present` crida el dibuix de la vista **tallat en
    aquest requadre**, i el que l'app hi pinta és la pàgina com la vol, **primitives incloses**. El sistema **no hi
    posa paper**: fer-ho seria pintar paper sobre una pàgina que algú altre repinta tot seguit, i el requadre es
    pintaria dues vegades —una rentada de `gl16` i el dibuix de l'app a sobre, dues passades de tinta per a un sol
    forat—. És el camí preferit i el que vol dir tenir un `on_draw` a la vista.
  - **Sense `on_draw`, el forat és paper** (0.173.2): el sistema **omple el requadre amb el paper de la pàgina**
    —el nivell de l'últim `ui.clear` de l'app, i blanc si no n'ha fet cap— i **demana a cada control que hi havia
    dins que es pinti una altra vegada**, amb buffer propi o sense. El requadre es dibuixa **amb el radi de la
    targeta** (dotze píxels), perquè les cantonades arrodonides són de la pàgina i no de la targeta. Una pàgina de
    targetes, botons, etiquetes i canvas torna sencera tota sola —el cas de `wipe`—; **el que hi hagi pintat amb
    primitives** (`ui.text`, `ui.rect`, `ui.line`…) **es perd**, perquè una primitiva no és cap control i no sap
    dibuixar-se.
  - **La recepta**: una app que pinta la seva pàgina a mà posa `ui.root().on_draw` i **no ha de fer res més** quan
    una targeta marxa —el sistema li demana el requadre i prou—. L'editor ho feia a mà (`ui.clear` i tornar a
    pintar-ho tot quan la targeta tancava); amb `on_draw` això és la mateixa funció que ja té (docs/30, docs/90).

### `ui.sparkline` i `ui.pie`: la forma d'uns números (0.74.0)

- **`ui.sparkline`**: una barra per mostra, la més vella a l'esquerra, i **l'alçada contra el pic de la finestra** i no
  contra un màxim fix —la forma es llegeix igual amb una escala de deu que amb una de deu mil, i això és el que es
  demana a un sparkline—. Una mostra que no és zero **sempre ensenya un píxel**: un u per cent en un gràfic de quaranta
  és menys d'un píxel, i una barra de res es llegeix com un zero.
- **`top` és la segona sèrie i es dibuixa buida**: quan les dues dades són parts d'un mateix total (el temps despert i
  el temps adormit d'un interval), la de baix és sòlida i la de dalt només la vora, i la proporció es veu abans de
  llegir cap número. És el que el vell va aprendre a `sparkline.h`.
- **`ui.pie`**: quatre porcions com a molt, els angles de la seva suma, i **el pastís comença a les dotze i gira cap a la
  dreta**. Els tons són `color`, `color+4`, `color+8` i `color+12` (tallats a 15): l'app els pot calcular igual per a la
  llegenda, i per això el control no torna cap número de color.
- **Les dues són de només mirar**: no són enfocables i no tenen estat. Una gràfica no es prem, i el que canvia són les
  dades, que les posa l'app.
- **Un gràfic no compara la seva taula**: dues taules de Lua no són mai la mateixa, així que assignar `samples` o
  `slices` és sempre un repintat. És el que vol una app que acaba de mesurar una cosa.

```lua
local spark = ui.sparkline(300, 60)
spark.samples = { 12, 18, 9, 24, 20, 31, 28, 33 }
spark:place(20, 100)

local pie = ui.pie(160, 160)
pie.slices = { 5, 3, 2 }
pie:place(20, 200)
ui.present()
```

### `ui.month`: el calendari (0.75.0)

- **Set columnes i sis setmanes sempre**: un mes que comença en diumenge en necessita sis, i una fila que apareix al
  març i desapareix a l'abril mou tot el que hi ha sota el calendari.
- **Els noms dels dies els posa l'app** (`weekdays`, set cadenes): com es diu un dimarts és una llengua, i la llengua
  no és del nucli. La setmana comença en **dilluns**.
- **Quin dia de la setmana cau el dia 1 i quants dies té el mes els sap el nucli** (`kernel/calendar.h`): és l'única
  còpia de la regla dels anys de traspàs de tot el sistema, i el dial s'atura a l'últim dia del mes perquè pregunta a
  la mateixa funció (docs/60).
- **Dos senyals**: el dia triat s'**inverteix** i el d'avui porta **marc**; en aquest vidre no hi ha color per a dues
  coses.
- **Les fletxes caminen els dies** i el centre en tria un amb `on_pick(self, day)`; **un dit tria el dia on és**, amb
  les mateixes cel·les que el dibuix.

```lua
local grid = ui.month(300, 300)
grid.year = 2026
grid.month = 11
grid.selected = 3
grid.today = 5
grid.weekdays = { 'dl', 'dt', 'dc', 'dj', 'dv', 'ds', 'dg' }
grid.on_pick = function(self, day) console.print('day ' .. day) end
ui.present()
```

### `ui.menu`: el menú contextual (0.76.0)

És el control que surt **sobre la pàgina quan una persona pregunta què es pot fer amb una cosa**. L'app el crea, li diu
les opcions i el col·loca on vol —normalment on és el dit, i per això és contextual—, i la resta és del sistema.

- **Les opcions són paraules i no icones**: una paraula diu què fa i no s'ha d'endevinar. I **no porta cap fila de
  «Tanca»**: el dit fora de la targeta i la tecla enrere ja són la sortida, i una fila de «Tanca» en un menú de tres és
  la quarta opció.
- **Una opció que l'app no pot fer ara s'amaga i no es mostra morta** (`hidden`): aquest vidre té dos tons i no hi ha
  manera de dibuixar una fila desactivada que es distingeixi d'una d'activa, i una fila que sembla que fa una cosa i no
  en fa cap és pitjor que no tenir-la. Una opció amagada **no ocupa lloc, no es pot tocar i el dial no hi passa**.
- **Una opció per caixa, i una columna abans de la següent**: un menú de quatre en una targeta ampla són dues columnes
  de dues, i les columnes es reparteixen senceres. **Cap caixa fa menys de 52 píxels d'alçada** —el mínim que aquest
  sistema es permet per a res que s'hagi de tocar amb el dit— i les caixes **creixen** fins a omplir la targeta: tres
  opcions en una targeta alta són tres portes altes.
- **La fila és una línia de paraules i no una tecla** (0.177.0): la paraula escrita sobre el paper de la targeta, i **la
  fila on és el dial, una banda de tinta d'extrem a extrem amb la paraula girada** —el menú contextual del projecte del
  paper, `MenuBar::draw()`, i no una pila de tecles amb la cara aixecada quatre píxels i una ombra a sota— (docs/92).
- **Dos senyals, com al calendari**: la fila del dial és la banda de tinta, i l'opció de `primary` porta **un filet d'un
  píxel**, que és tot el que queda del marc de tres d'una tecla. La fila del dial no en porta cap, perquè la banda ja ho
  diu tot.
- **`tag`** és una paraula curta **al final de la fila**, a la dreta («sys» per a una app del sistema, «lua» per a un
  guió), i va en el to `dim`, perquè el que una persona llegeix és l'etiqueta.
- **Les fletxes caminen les opcions que són a la targeta** (les amagades les salta) i **el centre en tria una** amb
  `on_pick(self, index)`, on `index` és el número de l'opció dins d'`items`. **Un dit tria la caixa que prem**, no la
  que tenia l'anella. **Tancar el menú és de l'app**: la regla de sempre, llegir l'acció, tancar i llavors fer-la.
- **Si `index` apunta a una opció amagada no es dibuixa cap anella** —l'app està assenyalant una cosa que no és a la
  targeta— i la primera fletxa la porta a la que hi ha en aquella direcció. I una targeta que no hi cap és una targeta
  que l'app ha mida malament: les opcions que no hi tenen caixa no es dibuixen.
- **Una targeta que apareix sota un dit que ja hi és es pressa amb aquell mateix dit**: el nucli el veu al torn
  següent, el prem i, en aixecar-lo, tria l'opció on és el dial —la primera—. Per això **un menú contextual s'obre quan
  el dit s'aixeca** i no quan toca (`apps/menu` ho fa així): un menú amb «Delete» a dins que es dispara perquè el dit
  que l'ha demanat encara hi era no és un detall.

```lua
-- Un menú contextual: surt on és el dit i fa una cosa amb allò que hi ha allà.
local card = ui.menu(300, 220)
card.title = 'Note'
card.items = {
  { label = 'Open', tag = 'lua' },
  { label = 'Rename', hidden = true },   -- res a renombrar encara
  { label = 'Delete', primary = true },
}
card.on_pick = function(self, index)
  console.print('option ' .. index)
  card:release()                         -- tancar-lo és de l'app
end
card:place(80, 200)                      -- o on era el dit quan es va aixecar
ui.present()
```

### `ui.menu_bar`: la barra de menús (0.77.0)

Una fila de paraules amb una línia a sota i **el menú que penja de la paraula que està desplegada**. És el control d'una
pàgina amb feina a fer —un editor, un gestor de fitxers— on les accions no caben en botons.

- **Cinc paraules i setze entrades com a molt**, i **un sol nivell**: una entrada no porta submenú, perquè on s'obre un
  submenú és una decisió de dibuix que aquest sistema no ha pres (el vell va fer la mateixa tria). Les paraules que no hi
  caben es reparteixen en columnes iguals, perquè una paraula amb el final tallat no la llegeix ningú.
- **El marc del control és tota la pàgina i el control només en pinta dues caixes**: la fila i el menú desplegat. La
  resta és de l'app, i per això una barra que dona tota l'alçada de la pàgina no esborra el que hi ha a sota (docs/90).
- **Una entrada que l'app no pot fer ara es desactiva** (`enabled = false`) i es dibuixa **en el to `dim`**, i **no
  s'amaga**: al contrari que al menú contextual, la fila es queda on és perquè el menú no canviï de mida el dia que
  torno a estar disponible. I **no respon ni al dit ni al dial**.
- **Una ratlla (`'-'`) és una línia entre dues entrades**, amb el seu aire: no és una fila que el dit premi ni una parada
  del dial.
- **Una X és una caixa amb el mig marcat** (`checked`): dos tons i cap gris, o sigui que «està encesa» és la caixa que té
  la X a dins i «està apagada» la que està buida. **Perquè la caixa buida surti hi ha d'haver el camp**, encara
  que sigui `checked = false`: una entrada amb el camp és un interruptor i una entrada sense el camp és una ordre, i
  així es distingeixen sense cap paraula. **Qui sap l'estat és l'app**: ella escriu `checked` i torna a donar la taula
  d'`items`, que és sempre un repintat.
- **El dit**: a un títol plega o desplega el seu menú (i el títol desplegat s'inverteix); a una entrada la posa al dial i
  **el deixar anar tria**. **El dial**: amb un menú desplegat, les fletxes caminen les entrades que es poden triar i el
  centre tria; **sense cap menú, la fletxa torna al recorregut del dial**, que és el que impedeix que una pàgina amb una
  barra sigui un cul-de-sac.
- **Una elecció crida `on_pick(self, title, entry)` amb el menú ja plegat**: la regla de sempre, llegir l'acció, plegar i
  llavors fer-la. `title` i `entry` són els números dins d'`items` del títol i del seu menú.

```lua
local bar = ui.menu_bar(500, 300)
bar:place(20, 40)
bar.items = {
  { title = 'File', items = { {label = 'Open'}, '-', {label = 'Save'},
                             {label = 'Quit', enabled = false} } },
  { title = 'View', items = { {label = 'Wrap', checked = true},
                             {label = 'Read Only', checked = false} } },
}
bar.on_pick = function(self, title, entry)
  console.print('menu ' .. title .. ' entry ' .. entry .. ' (open ' .. self.open .. ')')
end
ui.present()
```

### `ui.textbox_multiline`: el camp de diverses línies (0.78.0)

El germà d'`ui.textbox` amb lloc per caure-hi: **el mateix marc, el mateix cursor de bloc i el mateix cursor en bytes**.
El que canvia és què fa amb un text que no hi cap.

- **La finestra segueix el cursor**: el text no es desplaça sol, les files que es veuen són les que contenen el cursor, i
  quan el text és més curt que la caixa les files queden a dalt on les va escriure la persona. Un camp de diverses línies
  que deixa escriure fora del vidre és un camp on ningú veu el que tecleja (docs/90).
- **`wrap` és una manera de pintar i no un canvi del text**: apagat, una fila és una línia i una línia més llarga que la
  caixa es talla a la vora; encès, una fila es parteix a l'últim espai que hi cap —i l'espai que parteix no es dibuixa—.
  **No s'escriu ni un byte dins del text** de cap de les dues maneres.
- **On acaba una fila i on és el cursor surten d'una sola funció**, o sigui que el cursor del dibuix i el cursor del dit
  no es poden contradir; **el cursor és un bloc de tinta amb la lletra de sota en paper**, com al camp d'una línia.
- **El dit posa el cursor allà on toca** (0.78.0, afinat a la 0.85.2): la fila pel `y` i la lletra pel `x`, mesurada amb
  la font, que és l'única cosa que sap com n'és d'ample una lletra. Un punt passat el final d'una fila és el final
  d'aquella fila; **un punt per sota de l'última lletra és el final de tot el text** —la fila que el dit ha trobat no té
  cap lletra i el cursor no pot quedar-se al mig de l'última línia, que no és l'altura que s'ha tocat—, i **una fila que
  hi és i és buida és una fila**: el cursor hi va, a l'altura del dit. I el sistema **pinta el cursor que ha mogut ell**:
  un dit sobre el text no és una tecla, i la caixa no té cap `on_change` que avisi l'app, o sigui que la pàgina es
  presenta sola amb `gld16` (que neteja i no fa cap flaix) i el cursor del lloc d'abans no es queda al vidre.
- **Retorn escriu un salt de línia i no acaba l'edició**, que és el que fa en un camp d'una línia (`on_enter`). El text
  arriba sencer a `on_change(self, text)` després de cada tecla, i **`box:rows()`** diu quantes files s'hi veuen, preguntat
  a la mateixa aritmètica del dibuix. **`box:lines()`** diu quantes files té el text sencer, que és el que una app vol
  per dir «fila 12 de 40» a la seva línia d'estat (0.88.0).
- **Un text que s'acaba en un salt de línia té una fila més** (0.108.0): el cursor d'una caixa de text pot ser **darrere
  de l'últim byte**, i si aquest byte és un `\n` el cursor és al començament d'una fila que encara no té cap lletra —
  l'estat de qualsevol document just després de l'enter que va obrir-ne l'última línia—. Abans aquesta fila no es
  comptava, el cursor del final no pertanyia a cap fila del recorregut i es pintava **a la primera**, sobre les
  paraules del document, mentre el text creixia a sota. `box:lines()` ho diu: `one\ntwo\n` són **tres** files.

#### La barra del desplaçament i el gest (0.88.0)

**Quan el text no cap a la caixa, el cantó dret és una barra**: una pista d'un píxel de tinta amb un polze a dins, tan
llarg com la part del text que es veu, que camina pel que queda de pista segons la primera fila que es mostra. Dues
coses i no tres: la pista diu quant text hi ha i el polze on és la persona. **El text no passa mai per sota de la barra**
—la finestra es mesura amb el lloc que queda, i això el que fa és partir les línies llargues una mica abans—, i una
caixa on el text hi cap no en té, de barra (docs/90).

- **`top` és la primera fila que es veu i `-1` vol dir «la que demani el cursor»**: amb `-1` la caixa es comporta com
  sempre, i una app que escriu un número mana sobre el cursor, que és el que fa que una barra moguda pel dit no torni a
  la fila del cursor a la pintada següent. La caixa **cedeix `top` als límits del text**: una app no ha de saber quantes
  files hi ha per moure la finestra.
- **El gest el registra l'app** amb `box.on_swipe = function(self, rows) end`. Sense aquesta funció la caixa no té cap
  gest i el dit hi posa el cursor com sempre: un swipe sobre una caixa que ningú no ha preparat no ha de fer res.
- **El sistema fa l'aritmètica i pinta**: un dit que recorre la caixa paga una fila cada catorze píxels (els que fa una
  fila, que és el que diu `box`), escriu el `top` nou, presenta la pàgina amb `gld16` i llavors crida `on_swipe` amb les
  files que ha mogut —positives cap al final del text, negatives cap al principi—. Els píxels que no arriben a una fila
  es guarden per al moviment següent, o sigui que un dit lent no perd el que ha recorregut.
- **Un toc i un swipe no es confonen**: amb el gest registrat, el cursor **no** es mou en baixar el dit —encara pot ser un
  swipe— i es posa al lloc tocat quan el dit deixa anar sense haver-se mogut més de dotze píxels. Un dit que ha caminat
  prou ja no deixa cursor encara que torni al punt de sortida.
- **El gest és de la pàgina i no del sistema**: el començament del dit, els píxels que falten i el cursor que espera es
  netegen quan la pàgina entra i quan marxa, o sigui que una app que s'acaba amb el dit a sobre no deixa cap gest a mitges
  a la que ve després —la pressa de la segona és una pressa nova—. El `top` que el sistema ha escrit tampoc no viatja:
  les caixes són de la pàgina (docs/30, docs/90).
- **El gest només arriba a la caixa que té el focus, i mentre el dit és a sobre** (0.113.0): a cada moviment el sistema
  pregunta dues coses al control —que tingui `focused` i que el dit caigui dins del rectangle de dibuix de la caixa— i,
  si una de les dues falla, el gest s'atura allà i les files que havia recorregut **es perden**: una finestra que salta
  quan el dit torna és pitjor que una que s'ha quedat quieta. Una app que dona el dial a un altre control des
  d'`on_swipe` no veu com la primera caixa segueix movent-se, i un dit que surt de la caixa no la continua desplaçant.
- **Una caixa que es deixa anar es queda sense gest** (0.113.0): `box:release()` —i el col·lector, quan ja no la té
  ningú— oblida el contacte, i el dit que encara és al vidre **no prem res** fins que s'aixeca: la caixa que l'app
  construeixi al mateix lloc no rep el swipe d'un contacte que va començar a la que s'ha anat. El teclat que escrivia en
  aquella caixa també es queda sense objectiu.
- **El `wrap` compta a tot arreu, i és el que fa que un document es desplaci de debò** (0.113.0): la barra —la pista, el
  polze— i el desplaçament del dit compten **les files que es veuen** i no els salts de línia del fitxer, o sigui que
  amb `wrap = true` un paràgraf llarg són tres files i la finestra baixa fins a l'última de les tres. El compte del nucli
  ja ho feia abans de la 0.113.0; el que hi havia era que cap prova no ho deia, i
  `test_the_bar_and_the_window_of_a_box_of_lines_count_the_rows_of_the_glass` ho mesura amb el polze de la barra i amb
  l'última finestra.
- **La caixa és una parada del dial** (0.116.0, i era només del dit des de la 0.85.2): una placa amb tecles i sense
  tàctil ha de poder arribar a un camp, i el dial hi cau amb el **cursor a dins i sense banda** —la banda és el que
  compra entrar-hi—. Per això `ui.focus(box)` també funciona des de la 0.116.0, i per això una pàgina que només té
  camps ja no és una parada perduda.

#### Entrar a una caixa: el centre i les dues tecles (0.116.0)

**El dial s'atura a la caixa i el cursor hi és; el centre hi entra i la banda puja.** És la mateixa forma que té un
slider amb el seu valor: el control que té el focus es deixa moure per les dues tecles del dial, i un camp de text no és
una excepció. Un cop dins,

- **una caixa d'una línia** es mou una **lletra** a cada tecla —endavant i enrere—, i una lletra és un punt de codi i no
  un byte: el accent de «el meu fitxer» no es pot partir;
- **una caixa de diverses línies** es mou una **fila** a cada tecla, i la fila és la que el vidre ensenya: amb `wrap`
  una línia llarga són més files, i el cursor baixa de fila i no de línia. La columna es queda tan a prop com la fila
  ho permet, i a la primera i a l'última fila el cursor es queda on és;
- **la finestra segueix el cursor**: `top` torna a `-1` quan el cursor es mou amb una tecla, el mateix que quan s'escriu
  una lletra, perquè un cursor que ha sortit de la caixa és una persona editant una fila que no veu;
- **l'app ho sent a `box.on_move(self, cursor)`**, que és on la línia de sota la pàgina diu la fila. El sistema ja ha
  escrit `cursor` i `top` i pinta la pàgina després del ganxo: l'app no ha de presentar.

**Fora de la caixa (o abans d'entrar-hi) les dues tecles són del dial**, que és el que fa que una pàgina amb camps es
pugui recórrer. El centre torna a sortir de la caixa, i el cursor s'hi queda: el focus i el cursor són la mateixa cosa.

#### Un slider es toca després d'entrar-hi (0.117.0)

**El slider no pren les fletxes pel sol fet de tenir el focus.** Al principi era així —el control que té el dial es
deixava moure—, i el resultat és que una pàgina amb un slider es quedava sense les tecles de caminar: les fletxes li
arribaven sempre i el número es movia quan la persona volia anar al control de baix. Ara **el focus és el focus**:

- amb el slider enfocat, amunt i avall **continuen canviant de focus**, com sempre;
- **el botó central l'entra** (`slider.editing` és cert dins), i llavors les dues tecles són el número, de `step` en
  `step`, amb `on_change(self, value)` a cada moviment;
- **el botó central en surt** i les tecles tornen al sistema per moure el focus. L'app sent la premsa del centre a les
  dues bandes per `on_click(self)`, o sigui que un slider amb un callback no perd res;
- el `editing` **s'apaga sol** quan el dial se'n va a un altre control: qui torna ha de dir «entrar» una altra vegada.

**Els controls amb un centre que ja vol dir alguna cosa no entren**: un menú, una targeta, un calendari, una graella i
una llista trien alguna cosa amb el botó central, i treure'ls-hi aquest significat seria canviar dues coses de cop. La
regla viu en una funció sola (`ui_enters_to_move`) perquè afegir-hi un control nou sigui una línia i es vegi què ha de
complir: no tenir cap acció pròpia al centre.

#### Un camp que es llegeix i no s'escriu (0.116.0)

**`box.readonly = true` és una propietat del control i no una manera de dir-ho amb una altra**: la caixa **mai** aixeca
el teclat, ni amb el dit ni entrant-hi amb el centre, i **el cursor hi és igualment** —una pàgina que es llegeix és un
lloc on una persona és, i la línia de sota diu on—. Abans d'aquesta versió, una app ho havia de dir amb
`keyboard = false` i s'emportava el bloc amb el teclat (és el que feia l'editor). Les dues caixes, la d'una línia i la
de diverses, porten el camp.

**I `box.framed = false` treu la vora.** El control pinta la vora en `border` i l'interior en `background`, o sigui que
un marc del color de dins no és cap marc: la bandera dona el segon com a primer i el paper de la pàgina arriba fins a la
vora del control, que és el que vol un document que és tota la pàgina. És una bandera i no un quart color perquè el que
una app vol dir és «sense vora».

```lua
local box = ui.textbox_multiline(500, 300)
box:place(24, 80)
box.scale = 2
box.wrap = true
box.text = 'Notes\n\nThe enter key starts a new line here.'
box.cursor = 0
console.print('rows ' .. box:rows() .. ' of ' .. box:lines())
-- The finger scrolls the box and the bar says where in the text it is; written here, that is a setting of the page.
box.on_swipe = function(self, rows) console.print('moved ' .. rows .. ' to ' .. tostring(self.top)) end
local kb = ui.keyboard()
kb.target = box
kb:show('alpha')
box.on_change = function(self, text) saved = text end
ui.present()
```

### `ui.selector`: el camp que s'escull (0.79.0)

**La pregunta en el to `dim` i petit a l'esquerra, la resposta en tinta a la dreta, i una columna amb una fletxa a la vora
dreta: aquesta columna és la porta.** Un valor d'un conjunt conegut no vol un teclat: escriure una data i una hora
senceres en aquest vidre baixa la vista a 581 px, els dígits són petits i un dígit equivocat és un valor que sembla
correcte.

- **La llista no és al control**: el camp és una porta, i el dit o el centre disparen **`on_open(self)`**, on l'app posa un
  `ui.menu` (o un `ui.list` si les opcions són moltes) que dibuixa el sistema com qualsevol altre control. El sistema no pot
  guardar una taula que no és seva, i l'app ja la té.
- **El valor buit és un estat de debò**: `index = 0` i el camp dibuixa `empty` —el que l'app digui en la seva llengua— allà on
  aniria el valor. Un camp que sempre té valor menteix sobre què s'ha decidit.
- **Obert, el camp continua sent paper**: només la columna de la fletxa s'inverteix. Un camp que es torna negre de cap a
  cap es llegeix com que tota la pàgina s'ha apagat.
- **Dos tons i dues mides**: la pregunta és petita i grisa i la resposta és cos i tinta, i no un tercer color, que és el que
  manté el control servible amb un repàs `Du`.
- Qui escriu `index` és l'app (dins del `on_pick` del seu menú) i **qui apaga `choosing` també**: el control dibuixa i prou.

```lua
local hour = ui.selector(480, 56)
hour:place(24, 90)
hour.label = 'Hour'
hour.items = { '08', '09', '10' }
hour.empty = '-'
hour.on_open = function(self)
  local card = ui.menu(240, 200)
  card.items = hour.items
  card.on_pick = function(that, index)
    hour.index = index
    hour.choosing = false
    card:release()
    ui.present()
  end
  ui.present()
end
```

### `ui.notification_bar`: la banda dels avisos (0.80.0)

Una targeta d'una punta a l'altra per cada avís, amb títol, text i creu, **apilades cap amunt amb la més nova a baix**: és
la que queda més a prop del dit i la que marxa primer.

- **Tres com a molt al vidre, i el que no hi cap no es perd**: la banda ho diu amb un **`+N`** a sobre. Un avís que
  desapareix sense dir-ho és un avís que algú ha perdut.
- **La creu descarta pel camí d'entrada** (fer fora és una ordre i no un estat de prémer) i **la resta de la targeta obre
  pel camí de sortida**, com qualsevol altra tria del sistema. La creu sap quin avís és: `on_close(self, item)`.
- **El centre descarta la més nova**, perquè un avís que una placa sense tàctil no pot fer fora és un avís que es queda per
  sempre.
- **La llista d'avisos és de l'app**: el control els pinta i no els guarda. `table.remove(band.items, item)` i tornar a
  donar la taula és tot el que cal.

```lua
local band = ui.notification_bar(540, 240)
band:place(0, 672)
band.items = { { title = 'Battery', text = 'low' },
               { title = 'Saved', text = 'your note' } }
band.on_close = function(self, item)
  table.remove(band.items, item)
  band.items = band.items
  ui.present()
end
ui.present()
```

### `ui.icon_grid`: la graella de cel·les (0.81.0)

**Una icona amb el nom a sota, en pàgines.** És el control d'un menú de coses: **la marca de la caixa és el que es reconeix
d'un cop d'ull i el nom és el que ho confirma**, i per això un menú d'apps és una graella i no una llista.

- **La pàgina segueix el cursor**: l'app dona la llista sencera (48 cel·les com a molt) i **no gira mai una pàgina**; el peu
  diu on és (`2 / 3`) en el to `dim`, i el repàs és el mateix d'una llista.
- **Dos senyals com al calendari**: la cel·la `marked` s'**inverteix** (la caixa de la icona passa a tinta i la marca a
  paper) i la cel·la on és el dial porta **l'anella** dins de la cel·la.
- **El nom es queda en tinta encara que la cel·la estigui triada**: seu sobre el paper de la pàgina i no dins de la caixa de
  la icona, i per tant no segueix la inversió. Una cel·la triada amb el nom en paper sobre paper seria un nom invisible.
- **La icona és una caixa amb una paraula curta a dins** (`mark`): aquest nucli no té cap mapa de bits propi —una taula de
  bytes per icona és una cosa que ningú no pot comprovar llegint-la— i una app que vulgui una imatge de debò ja té
  `ui.image` en un canvas seu.
- **Les fletxes caminen les cel·les** i al primer i a l'últim la tecla torna al recorregut del dial; el centre i el dit trien
  amb `on_pick(self, index)`. **Un dit tria la cel·la que prem**, no la que tenia l'anella.
- **`columns = 0`** deixa que el control decideixi les columnes per l'amplada, amb noranta-sis píxels com a cel·la més estreta:
  dues meitats d'un objectiu no són dues coses que es puguin prémer.
- **Moure l'anella costa dues cel·les i no la pàgina** (0.177.3): quan el dial camina dins la graella, quan el recorregut
  li dona el focus i quan l'hi treu, l'únic que canvia és l'anella, i el que s'ha de pintar és el rectangle que ocupen la
  cel·la que deixa i la que entra —les dues senceres, perquè el paper de la graella tapa l'aire del mig—. Una cel·la que
  salta de pàgina sí que és el control sencer: les cel·les es tornen a col·locar i el peu diu un altre número. I en una
  graella de controls de l'app (`ui.grid_entry`) no s'ha de pintar **res**: cada cel·la pinta la seva anella ella mateixa,
  i una app que invalidés la graella a cada canvi de focus estava pintant les nou icones per una anella.

```lua
local grid = ui.icon_grid(500, 400)
grid:place(20, 60)
grid.items = {
  { label = 'Notes', mark = 'N' }, { label = 'Clock', mark = 'C' },
  { label = 'Files', mark = 'F' }, { label = 'Paint', mark = 'P' },
}
grid.marked = 1
grid.on_pick = function(self, index) console.print('cell ' .. index) end
ui.present()
```

## El que costa, contat a la porta

**`stats` no és una línia de log i no porta etiquetes**: és una taula, i la llegeix una persona. Les
columnes són fixes i alineades (`meter`, `calls`, `total`, `mean`, `last call`) i cada temps va en
l'escala que es llegeix.

**Cada crida que arriba a un dispositiu es conta i es cronometra**, i també el moment en què es va fer
l'última. No és un registre de depuració que s'encén: és una taula fixa que sempre hi és i que el prompt
ensenya amb `stats` (docs/30, docs/50, docs/75). I **cada fila hi és encara que no s'hagi cridat mai**: un
zero és una mesura —vol dir que aquell aparell no ha rebut ni una petició des de l'últim `stats reset`— i les
quatre columnes de temps d'una fila sense crides són un guió, perquè no hi ha cap crida de la qual parlar. El
que **no** hi és és el que aquest build i aquesta placa no tenen: un ATOM no té fila de vidre i la Paper no
la té de llum (docs/50, docs/85).

```
lunok> stats
meter           calls      total       mean        min        max       last
boot                1    108.2ms    108.2ms    108.2ms    108.2ms    108.0ms
flash.read         36     83.5ms      2.3ms      2.1ms      4.0ms      2.16s
flash.list          3    136.4ms     45.4ms      2.4ms     80.0ms      2.15s
flash.stat         71      517us        7us        2us       24us      2.15s
sd.read             0        0us          -          -          -          -
glass.flush         4        2us        0us        0us        1us      3.20s
touch.read          2      1.7ms      866us      860us      872us      3.20s
clock.now           1        0us        0us        0us        0us      3.20s
clock.mono          9        3us        0us        0us        1us      4.20s
clock.wake          0        0us          -          -          -          -
keys.read           4        1us        0us        0us        1us      4.20s
kv.get              1       45us       45us       45us       45us        0us
kernel.loop         4      4.00s      1.00s      1.00s      1.00s      4.20s
lua.run             1     40.1ms     40.1ms     40.1ms     40.1ms      2.20s
lua.own             1        6us        6us        6us        6us      2.20s
psram.alloc         3       31us       10us        0us       25us      2.16s
power.battery       0        0us          -          -          -          -
env.read            0        0us          -          -          -          -
lua.call            2     40.3ms     20.1ms      177us     40.1ms      2.20s
layers: 1 calls into the system, 40.1ms in them
turns 4, kernel 4.00s of 4.33s up, idle 334.3ms
lua 40.1ms: 19us the app's own code (the rest is calls into it)
doors 220.5ms (5.0 % of the uptime)
```

**Què hi ha a la taula, i què no.** Hi és tot el que es pot comptar a la porta:

| Grup | Files |
|---|---|
| Arrencada | `boot` — el sistema des del primer byte fins al prompt |
| Disc | **cinc unitats × quatre portes**: `flash.*`, `sd.*`, `nvs.*`, `rtc.*`, `fram.*`, cadascuna amb `.read`, `.write`, `.list`, `.stat` |
| Vidre | `glass.flush` (totes les pintades), `glass.gc16` (les que netegen), `glass.idle` |
| Tàctil | `touch.read` |
| Rellotge | `clock.now`, `clock.mono`, `clock.wake` |
| Tecles | `keys.read` |
| Ajustos | `kv.get`, `kv.set` |
| Nucli i intèrpret | `kernel.loop`, `lua.run`, `lua.own`, `lua.call` |
| Memòria extra | `psram.alloc`, `psram.free` |
| Energia | `power.battery`, `power.sleep`, `power.cpu` |
| Llum | `led.show` |
| Sensors | `imu.read`, `env.read` |
| Ràdio | `net.wifi`, `net.http`, `net.ntp` |

**La llista és la d'aquest build i aquesta placa**: `sd.*` i `fram.*` només hi són on hi ha la ranura i el
xip, `glass.*` i `touch.read` només a la Paper, `led.show` només als ATOM, `imu.read` només al Matrix,
`env.read` i `power.battery` a la Paper, `net.*` quan el build porta ràdio i `psram.*` quan la placa té
memòria extra. I **cada fila que hi és surt sempre**, amb zero si no s'ha cridat: una fila que falta és una
fila que no existeix en aquesta placa, i una fila a zero és una fila que existeix i no s'ha fet servir.

- **Cada temps va en l'escala que es llegeix** —`845us`, `12.3ms`, `1.98s`, `3m07s`, `1h23m`— i mai en un
  compte de mil·lisegons que s'hagi de dividir al cap. Sense espais, perquè la taula ha de capir en
  vuitanta columnes amb sis columnes de números.
- **Les tres xifres d'una fila**: quantes vegades, quant de temps en total (i la mitjana), i **quan va
  ser l'última**, comptat des de l'últim `stats reset` i no des de l'encesa: un número absolut d'una
  placa que fa hores que està encesa no el llegeix ningú. I **el mínim i el màxim d'una sola crida**,
  perquè una mitjana de dos mil·lisegons pot ser una crida de quatre i mil d'un: són dos problemes
  diferents i la mitjana els amaga.
- **Una porta i una unitat, un comptador.** El sistema de fitxers té una fila per unitat i per porta
  perquè una targeta i una flash no costen el mateix i un sol número amagaria el que importa (docs/70).
  I el rellotge també: el **comptador monòton** (`clock.mono`) és una lectura del xip RTC com l'hora, i
  el botó, la llum i la ràdio el demanen cada torn.
- **`lua.call` és cada crida d'una app a l'API** —`fs.read`, `ui.rect`, `sys.delay`, totes— amb el ganxo
  del propi intèrpret i sense un tanc per funció: l'ATOM dona 64 KB a una app i els tancs en menjarien
  cinc. I com que l'intèrpret es fa de nou a cada app, la fila **`app <id>`** dona la diferència des que
  la seva va arrencar — això és el «per app».
- **I una crida que torna una promesa es mesura fins que la promesa es resol**, no fins que la crida
  torna. `sys.delay(50)` torna de seguida i no costa els vuit microsegons de la trucada: costa els
  **cinquanta mil·lisegons** que l'app va esperar. Mesurat a l'emulador: `lua.call 2 51.1ms` i
  `probe: 1 calls into the system, 50.8ms in them`.
- **El compte és complet, i res no es conta dues vegades.** Les tres línies del final ho diuen:
  - `turns N, kernel X of Y up, idle Z` — els torns del nucli, el que hi ha costat, i el que queda de
    vida sense el nucli donant voltes.
  - `lua R: O the app's own code` — el torn de l'app sencer, i la part que és codi seu (la resta són
    crides al sistema).
  - `doors D (P % of the uptime)` — la suma de les portes. **Els medidors que en contenen d'altres**
    (`boot`, `kernel.loop`, `lua.run`, `lua.own`, `lua.call`) **no entren a la suma**, o el mateix
    microsegon es comptaria tres vegades i el total passaria del cent per cent (va passar: 138 %).
- **La vida del sistema es compta des de l'arrencada** (el rellotge dels comptadors comença amb el
  `boot`) **o des de l'últim `stats reset`**. `stats reset` posa la taula i el rellotge a zero: la
  mesura és d'una tirada i no d'una vida.
- **El que encara no hi és**: el pic de memòria d'una crida i el repartiment de les pintades per cada
  mode (avui només `gc16` té fila pròpia).

## El log

**Totes les línies del sistema tenen la mateixa forma**, les escrigui el nucli o un conductor:

```
[I][CFG] /cfg on littlefs (asked for littlefs)
[I][VFS] 9 mounts
[I][BOOT] board=m5stack-paper caps=14/19
[I][UI] view 540x912 at 0,48, 246240 bytes of canvas
[W][I2C] read of 5d failed (ESP_FAIL)
```

- **`[nivell][etiqueta] missatge`**: el nivell és una lletra (`D`, `I`, `W`, `E`) i l'etiqueta és el
  conductor o la part del sistema que parla —`PANEL`, `TOUCH`, `SD`, `I2C`, `WIFI`, `POWER`, `FS`,
  `VFS`, `APPS`, `UI`, `METER`…—. Una línia sense etiqueta no existeix.
- **El log es mira a la consola mentre el sistema arrenca** (`verbose off` el calla) i es queda al
  **cercle de 2 KB** que `log` aboca. Els conductors no depenen del nucli i no poden cridar-lo: formategen
  la línia i la donen a un *sink* que el nucli instal·la, i per això la línia d'un conductor acaba al
  mateix cercle que les altres (docs/50, docs/85).
- **Una línia diu números**: quants bytes, quants mil·lisegons, quina adreça. Una frase sense cap número
  és una línia que no serveix per a res quan la placa és a la mà.

## `touch`

El vidre de la placa: el GT911 de la Paper (docs/50). Tres crides i cap promesa, i és una decisió: un
dit és un **estat que es llegeix en un bucle**, no una cosa que una app espera, i una app que espera un
toc és una app que no fa res més.

| Crida | Torna | Notes |
|---|---|---|
| `touch.caps()` | `{points, width, height}` | Quants dits alhora i **la mida de la vista de l'app**, que és el marc en què compten les coordenades (`ui.caps()` diu el mateix) |
| `touch.read()` | taula de `{id, x, y, size}` | Els dits que hi ha **ara**, de la darrera lectura del nucli (el nucli pregunta al xip a cada torn). Taula buida quan no toca ningú, no `nil`, perquè un bucle la pugui recórrer sense preguntar |
| `touch.gesture()` | `{kind, x, y, dx, dy, spread, held_ms, fingers}` o `nil` | L'últim gest, i **es pren una vegada**: preguntar-ho dues vegades no el repeteix |

- **Un fotograma que no ha arribat no és un dit que s'aixeca** (3/11). El xip entrega el fotograma
  d'un escaneig **una vegada** i el driver se'l queda, o sigui que qui pregunta més de pressa que el
  vidre llegeix «no hi ha ningú» entre dos fotogrames del mateix dit. El nucli llegia aquests buits com
  un aixecament: **un sol toc en una targeta triava dues opcions**, un botó aguantat es deixava anar
  sol, i el gest sortia com una fila de tocs. Ara el fotograma que no arriba **manté el que hi havia**
  fins que el vidre calla un escaneig sencer (30 ms, tres dels deu del xip); el gir del fotograma es fa
  només quan n'arriba un de nou, perquè es fa sobre el mateix buffer i girar-ne un de guardat el girava
  un cop per torn (el punt anava marxant sol).

- **Els gestos els fa el sistema, no el xip.** El GT911 té gestos propis i ni el driver de M5 ni el
d'Espressif els llegeixen. Aquí són **formes del flux de punts**: `tap` (cau i s'aixeca en menys de
400 ms sense moure's més de 20 px), `double` i `triple` (dos i tres tocs seguits, amb 350 ms entre un
i el següent), `long` (aguantat un segon, dit quan es creua el llindar i no en aixecar-se), `drag`
(**amb un dit o amb dos**, i `fingers` ho diu: dos dits que es mouen junts són un pan, amb `dx`/`dy`
del punt del mig), `two` (el segon dit arriba, amb el punt del mig) i `pinch` (els dos dits s'han
apropat o separat: `spread` és quants píxels, positiu cap enfora). Volen dir el mateix a qualsevol
vidre i es poden provar al PC (docs/85).
- **Un toc es diu 350 ms després de llevar el dit**, i és el preu de `double` i `triple`: fins que la
  finestra no es tanca, un toc no se sap si és sol. L'altra manera —dir-lo de seguida i després
  esmenar-lo— faria que cada app desfés el que ja havia fet amb el primer toc. El `triple` sí que surt
  de seguida, perquè no hi ha res més llarg que tres.
- **Un gest de dos dits es diu pel més gran dels dos que pot fer**: si la distància ha canviat més que
  no s'ha mogut el mig, és `pinch`; si s'ha mogut més el mig, és `drag` amb `fingers = 2`. I **els dos
  números van sempre al missatge** (`spread` i `dx`/`dy`), així que una app que vulgui la història
  sencera no depèn del nom.
- **El pessic de gran a petit és del sistema i vol dir «tanca»** (0.163.0). El gest de tancar no és de
  cada app: **el sistema el pren de la cua abans que l'app el llegeixi** i fa una de tres coses. Si
  l'app té **vistes a sobre de la primera** (`app.view_push`), baixa la de dalt i l'app rep
  `on_back(depth)` perquè torni a pintar la pàgina de sota. Si **no en té cap**, tanca l'app, i la
  pàgina que la va obrir torna a la pantalla igual que amb els tres tocs. I si l'app és **un
  llançador** (`"launcher": true` al manifest), **no es tanca**: una placa sense cap pàgina és una
  placa que s'ha d'anar a buscar amb un cable. El llindar és de **60 píxels** de tornada, que és el pas
  que el projecte del paper donava als seus pessics; **un pessic que obre els dits, o que torna menys
  de seixanta píxels, no és del sistema** i l'app el llegeix amb `touch.gesture()` com sempre.
- **I les altres maneres de sortir fan el mateix camí** (0.163.1): els **tres tocs de la tecla del mig**
  i els **tres tocs al vidre** també baixen una vista i només tanquen quan no en queda cap —una
  pàgina amb una altra a sobre no és l'app—. És una sola funció del nucli (`shell::go_back_one_view`) i
  tres qui la criden, perquè «tornar enrere» vol dir una cosa i tres llocs que la decideixin són tres
  llocs que es poden contradir. La línia del registre diu quin dels dos ha passat:
  `button: three taps, back one view` o `button: three taps, closing the app`. **El llançador és
  l'excepció del pessic i no de les tecles**: tres tocs de la tecla del mig tanquen un llançador, perquè
  aquest gest és la sortida d'una pàgina que s'ha deixat de sentir (i a la pàgina d'inici no hi arriba:
  una graella de cel·les és enfocable i la tecla del mig hi és Enter).
- **Mentre hi ha dos dits, la màquina d'un dit està aturada**: un `pinch` no és un `drag` del dit que
  la placa llista primer, ni una premuda llarga de dos dits és un `long`. Mesurat a la Paper l'1/11,
  que era exactament el que passava abans d'arreglar-ho (docs/50).
- **`size` no és una pressió**: és l'àrea que el xip mesura sota el dit. Dos dits que premen igual de
  fort donen dues mides, i no s'inventa cap escala (docs/50).
- **`read()` no torna a preguntar al xip**: el vidre dona un frame una vegada i qui el pren el neteja,
  o sigui que el nucli el llegeix al seu torn i la crida dona aquesta còpia.
- **Si el dit cau sobre un control enfocable, el sistema el rep primer**: el control agafa el focus i es
  posa `pressed` en baixar, i el clic arriba en aixecar-lo a dins; l'app continua veient el mateix frame i
  el mateix gest a `touch.read()` i `touch.gesture()`, perquè el control no amaga el dit, només decideix
  què fa el clic (docs/30, docs/90).
- **Sense vidre**: les tres crides responen `nil, E_UNSUPPORTED` i `sys.caps().touch` diu `false`.
- **El marc és el de la vista de l'app, i no el del xip.** El xip compta en 540 × 960 i la pàgina que
  veu una app també, i a sobre **del dret**: es va mesurar l'1/11 tocant les quatre cantonades (docs/95,
  E-6) i el dit arriba on s'ha posat, amb un sol gir que amb la pàgina de fàbrica (270) val zero. La
  conversió la fa el sistema en un sol lloc (`turn_frame()` de `lib/kernel/src/touch.cpp`) i el `dx`/`dy`
  dels gestos surten ja girats: **una app no ha de girar mai res**. Un dit que cau a la barra arriba
  filtrat a la vora de la vista i no desapareix (docs/50).
- El que llegeix el panell en bucle i escriu els dits i els gestos ho té al davant: `ui.touch()` torna la
  posició i `ui.touch_progress()` el gest que s'hi està fent.

## `imu`

El sensor inercial: **el Matrix en porta un** (MPU6886, a la I2C interna, docs/50) i l'ATOM Lite no.
Una sola crida, perquè les tres coses que contesta el xip arriben en la mateixa lectura i una app que
es vol orientar les vol totes alhora.

| Crida | Torna | Notes |
|---|---|---|
| `imu.read()` | `{ax, ay, az, gx, gy, gz, celsius}` o `nil, error` | Acceleració en **g**, gir en **graus per segon** i la temperatura del xip en **°C** |

- **En una placa que no en té, `E_UNSUPPORTED`.** La crida hi és igual i el que no fa mai és tornar
  zeros: un zero vol dir «pla i quiet», i una app no podria distingir-lo de «aquí no hi ha res».
- **`E_IO` si el xip no contesta.** És un xip en un bus compartit i pot no respondre; llavors la crida
  torna l'error en comptes de quedar-se esperant.
- **Torna de seguida i no és una promesa**: la lectura és al mateix xip i no hi ha res a esperar. Una
  app que la vol cada 100 ms ho diu ella amb `sys.delay(100):await()`.
- **Les tres coses de la mateixa lectura** són a posta: agafar acceleració i gir en dues crides és
  dues mostres de dos instants diferents, i una app que calcula una orientació no ho vol.
- **Els eixos són els del xip, i el sentit de cada un en aquesta placa encara no s'ha mesurat** al
  maquinari (pendent). Una app que en depèn ho ha de poder comprovar en un segon, i per això la que
  hi ha, `tilt`, **ensenya els números a la consola** a més de pintar el llum.
- **No és de nou eixos**: no hi ha brúixola ni magnetòmetre, o sigui que l'orientació que se'n pot
  treure és la de la gravetat i la del gir, no la del nord.
- **El sensor no és del sistema i no té política pròpia**: només es llegeix quan una app ho demana. No
  hi ha res engegat en segon pla ni cap mostreig periòdic.
- Amb `sys.caps().imu` una app sap si tot això existeix abans de demanar-ho.

## `button`

El botó: **l'única cosa que una persona pot prémer** en aquesta placa (docs/50).

| Crida | Torna | Notes |
|---|---|---|
| `button.count()` | enter | Quantes tecles té la placa: **1** a l'ATOM i **3** a la Paper |
| `button.list()` | taula | **Totes** les tecles descrites: `{key, face, spot, kind, group}` de cadascuna. És el que una app llegeix un cop a l'inici |
| `button.info(key)` | taula o `nil, error` | La mateixa descripció d'una tecla; una que no hi és dona `E_INVAL` |
| `button.wait([ms])` | **promesa** | Es resol amb `{key, kind = "press"\|"long", held_ms}` de la primera tecla que es premi; **20 000 ms** per defecte |
| `button.down(key)` | booleà | Aquesta tecla és premuda **ara** (es llegeix en el moment de preguntar-ho) |
| `button.held(key)` | enter | Quants ms fa que és premuda, o `0` si no ho està |

```lua
local press = button.wait(10000):await()
console.print('kind ' .. press.kind .. ', held ' .. press.held_ms .. ' ms')
```

- **La tecla 0 és la principal**: a l'ATOM és l'única que hi ha i a la Paper és la **central**, la
  del mig del dial. Les altres són de l'app, excepte en una pàgina amb controls enfocables: allà el
  sistema les fa servir per passar d'un control a l'altre (vegeu `UIButton`, més amunt).

### On és cada tecla, i com està feta

Una app no ha de saber números de GPIO ni de tecles: el que ha de saber és **on** és la tecla i **què
és**. El sistema ho diu amb les mateixes paraules a totes les plaques:

| Camp | Valors | Què vol dir |
|---|---|---|
| `key` | enter | El número de tecla, el mateix que es passa a `down`/`held` |
| `face` | `"front"`, `"back"`, `"left"`, `"right"`, `"top"`, `"bottom"` | **La cara de la caixa** on és: la placa és una capsa de sis cares |
| `spot` | `"centre"`, `"up"`, `"down"`, `"left"`, `"right"` | **On, dins d'aquella cara**: el mig i els quatre costats |
| `kind` | `"simple"` o `"compound"` | **Com està feta**: una tecla sola, o diverses d'un sol component |
| `group` | enter | Les tecles del **mateix component** comparteixen número de grup |

Per què serveix: una app dona **una funció a una posició**, no a un número. La del mig de la cara és la
mateixa posició en un botó sol que en un dial de tres, i el que fa amunt i avall no ha de canviar
perquè la placa tingui una tecla o tres:

```lua
local keys = button.list()          -- {key, face, spot, kind, group}
for i = 1, #keys do
  local key = keys[i]
  if key.spot == 'up' then ... elseif key.spot == 'down' then ... end
end
```

- **`kind` no és un detall**: el botó de l'ATOM i el dial de la Paper són el mateix contacte i no són
  la mateixa cosa de tocar. `"compound"` diu que aquelles tres tecles són **un sol component**, i
  `group` diu **quin**: una placa amb dos botons separats i un dial tindria dos grups.
- **Les posicions són les de la placa i no les de la pantalla**: la tecla és on és el contacte, i la
  rotació de la vista (`ui.rotation`) no la mou (docs/50).
- `button.list()` torna una taula buida en una placa sense tecles, i `button.info` d'una tecla que no
  hi és dona `E_INVAL`: cap de les dues inventa res.
- **Si la pàgina té controls, el dial és del sistema**: `amunt`/`esquerra` van al control anterior,
  `avall`/`dreta` al següent i `centre` activa el que té el focus, amb la volta completa. Aquestes
  premudes **no arriben** a `on_button`; l'app les rep com a `on_focus` i `on_click` del control. Si cap
  control no és enfocable, el dial torna a ser tot de l'app i res d'això no canvia.
- **Hi ha tres maneres de rebre una premuda i les tria l'app**:
  1. **Esperar-la** amb `button.wait(ms)`, que és una promesa: l'app no es congela, el nucli segueix
     girant i la resta del sistema funciona (docs/30).
  2. **Rebre-la** amb el hook **`on_button(kind, held_ms, key)`**, que es crida entre dos torns amb la
     coroutine suspesa —exactament com `on_alarm`— i per tant **no pot esperar**. La tecla va **al
     final** a posta: una app escrita per a l'ATOM que declari `on_button(kind)` o
     `on_button(kind, held)` segueix funcionant.
  3. **Preguntar-la** amb `button.down(key)` i `button.held(key)` dins del bucle de l'app, que és el
     que cal per repetir mentre una tecla es té premuda.
- **Si hi ha una espera oberta, guanya l'espera**: el hook només es crida quan ningú està esperant, i
  així una app no rep dues vegades la mateixa premuda.
- **`kind` és `"press"` o `"long"`**: `"long"` arriba **una vegada**, quan la premuda passa d'**1 s**, i
  no quan el dit s'aixeca.
- **El sistema es queda el gest de tancar, i només de la tecla principal**: **tres tocs dins d'1,5 s
  tanquen l'app del davant**, i es compten **abans** d'avisar l'app, o sigui que cap app no s'ho pot
  empassar. És el mateix gest que tres ESC a la consola, amb la mateixa finestra, perquè una persona no
  n'ha de recordar dos. El tercer toc **no arriba** a l'app: se la tanca. **Tres tocs d'una tecla
  lateral no tanquen res**: són d'un menú, no d'una sortida. **En una pàgina amb controls enfocables el
  centre és Enter** i els tres tocs són tres activacions, no una sortida: la sortida d'emergència és
  `ESC` o un control `Quit` de l'app (docs/90).
- **La repetició no és del sistema, és de l'app.** Què fa una tecla que es té premuda depèn del que fa
  la tecla —un teclat repeteix lletres, una tecla de volum repeteix passos, una de menú no repeteix
  res— i per això el sistema només diu **si** és premuda i **quant fa**; el ritme el posa l'app. L'app
  `keys` en fa l'exemple: el primer repetit als **500 ms** i després cada **125 ms**, que és el ritme
  d'un teclat. I pregunta `button.count()` abans de dibuixar res, o sigui que la mateixa app funciona
  amb l'única tecla de l'ATOM i amb les tres de la Paper.
- **Sense app al davant no passa res**: les premudes no es compten ni es guarden, i una app que arrenca
  no es troba un gest a mitges (docs/30).

## `power`

| Crida | Torna | Notes |
|---|---|---|
| `power.caps()` | `{sleep_light, sleep_deep, battery, charging, wake}` | Un bit només és cert si la placa ho té de debò; `wake` diu de **què** es pot despertar (`"timer, button"`, `"timer"`, `"timer, clock"` o `"none"`) |
| `power.battery()` | `{mv, pct, charging}` o `nil` | `nil` és **"aquesta placa no ho sap"**, i no és el mateix que zero volts. `mv` és una mesura i `pct` una **corba** (una cel·la de liti no és una línia recta, `hal/battery.h`); `charging` només és cert si la placa ho pot preguntar, i al Paper i a l'emulador és sempre fals. **De mà**: `lua` amb `power.battery()` ensenya els dos camins |
| `power.sleep(opts)` | **promesa** | `{mode="light" o "deep", in_ms=<ms> o until=<època en segons>}`; es resol `true` quan la placa torna |
| `power.keep_awake(b)` | booleà | L'app diu que està fent una cosa que no s'ha d'interrompre |
| `power.cpu()` | número | La velocitat del processador, en MHz |
| `power.cpu(mhz)` | número o `nil, error` | La canvia. **Els passos són 80, 160 i 240**: un altre número es refusa amb `E_INVAL` i **no s'arrodoneix** |
| `power.off()` ⏳ | booleà | Apaga la placa de debò: talla el corrent, no arma res i només el botó la torna a encendre. **Al prompt ja hi és** (`poweroff`, que diu si la placa no en té el control, docs/50); des de Lua encara no —`sys.reboot()` sí que hi és des de la 0.171.0, i aquesta és la que li falta— |

- **Un son necessita una hora per tornar.** Sense `in_ms` ni `until` no hi ha res per amollar i es
  refusa amb `E_INVAL`: una placa que dorm sense hora espera un dit al botó (docs/60). També es
  refusa si la placa no sap quina hora és: primer `ntp` o algú que la posi.
- **`in_ms` i no `in`**: `in` és una paraula reservada de Lua i una taula amb `in=1000` ni tan sols
  s'analitza (`unexpected symbol near 'in'`, mesurat el 31/10).
- **El son lleuger torna i la promesa es tanca** amb `true`: la memòria, l'intèrpret i la consola hi
  són, i l'app continua exactament on era — però **pot ser abans del que havia demanat**: un botó el
  talla, i també la propera alarma de la cua, que és del sistema i no ha de fer cua darrere d'una app
  que dorm. L'app que necessiti el descans sencer l'ha de tornar a demanar (docs/60).
- **Abans de dormir, la línia s'ha buidat**: el que s'hagi escrit per la consola surt sencer i no
  tallat a mitges, perquè la UART s'atura amb el xip (docs/50).
- **El son profund no torna.** Abans de parar el xip, el sistema **desa quina app hi ha al davant** i
  la tanca amb els seus comiats (`on_pause`, `on_save`, `on_close`); en arrencar, la torna a obrir
  amb **`app.args.restore = true`** perquè llegeixi el que va desar (docs/70, `fs.data`). Per això el
  que s'ha de recordar ha d'anar a **disc**: la RAM no sobreviu.
- **La promesa del son profund no es tanca mai**, i no és un defecte: l'app ja no hi és quan el xip
  torna. L'app que vol fer feina en despertar-se la fa a l'arrencada, mirant `app.args.restore`.
- **Al Paper, un son profund és una apagada**: la placa deixa de tenir corrent i el rellotge la
  torna a encendre a l'hora demanada (docs/50). Què es pot esperar: el xip **compta al segon** fins a
  4 h 15 min (el seu comptador de compte enrere) i, per a esperes més llargues, al minut (l'alarma de
  rellotge). Més d'un dia s'arma **en tandes**: la placa es desperta abans d'hora, no dona l'app per
  desperta —ni la torna a obrir—, i s'apaga un altre cop fins a l'hora bona. Una placa amb el cable
  posat sí que cau al son del xip, que és l'únic que pot funcionar-hi (docs/60).
- **`power.keep_awake(true)`** atura la política d'oci: el sistema **sí que es dorm sol** quan la
  placa té un xip de rellotge, hi ha una alarma pendent i ningú no la fa servir (docs/60), i això és
  el que l'app pot impedir mentre està fent una cosa que no s'ha d'interrompre.
- **On es desperta cada banda**: l'ATOM pel **temporitzador del RTC** i pel **botó** (`GPIO39`); el
  Paper pel temporitzador del RTC i, quan està apagat, per **l'alarma del rellotge** (`"timer,
  clock"`); el PC no dorm: mou el rellotge (docs/85). L'emulador **no es reinicia** després d'un son
  profund, i per això les proves criden `kernel::power::restore_session()` a mà.
- **`power.cpu` va de 80 a 240 i prou.** Per sota de 80 l'APB baixa amb la CPU i **cap perifèric del
  core es torna a configurar** (no hi ha ni un `onApbChangeCallback` registrat a `cores/esp32`): el
  port sèrie sortiria amb el baud malament, i és l'única interfície d'aquesta placa. El valor que
  torna és **el que la placa corre de debò** i no el que se li ha demanat: un xip classificat per a
  160 contesta 160 quan se li demanen 240. La placa arrenca a **160 MHz** i una app que vol anar de
  pressa ho ha de demanar ella, perquè el sistema no el canvia sol (docs/90).

```lua
-- Es desa el que calgui abans de baixar, i es llegeix en tornar.
function on_save()
  local file = fs.open(fs.data('estat.txt'), 'w')
  if file then file:write(page) file:close() end
end

if app.args and app.args.restore then
  page = tonumber(fs.read(fs.data('estat.txt'))) or 1
end

power.sleep({ mode = 'deep', in_ms = 300000 }):await()   -- cinc minuts
```

El que dorm cinc minuts, desa quantes vegades ho ha fet i ho torna a llegir quan la placa arrenca és
`env` (docs/46). I el que es mesura amb un son és **quant s'ha desviat el despert** de l'hora demanada:
`rtc.now()` abans d'adormir-se, `rtc.now()` en tornar, i la diferència contra els mil·lisegons que es van
demanar.

## `log` ⏳

**Avui no hi és.** El contracte: un anell de línies que una app pot escriure i tornar a llegir, sense
arribar mai a la flash.

| Crida | Notes |
|---|---|
| `log.debug(fmt, ...)` / `log.info` / `log.warn` / `log.error` | Sintaxi de `string.format` |
| `log.dump([n])` | Torna les últimes `n` línies de l'anella com a cadena |

El log no arriba mai a la flash. Si l'app té el port sèrie, el log **no** surt per pantalla: queda a
l'anella i es pot llegir amb `log` des del prompt o amb `log.dump()`.
