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

## Què hi ha avui (30/10)

El contracte d'aquest document és el sencer; el codi encara no. Ja hi són:

- `print(...)`, `console.print(...)` i `console.write(s)`.
- `sys.name`, `sys.version`, `sys.board`, `sys.emulator`, `sys.lua` (valors, sense cridar res).
- `sys.heap()`, `sys.gc(mode)`, `sys.time_ms()`, **`sys.delay(ms)`** i **`sys.caps()`**.
- **`console.read_line()`** i, amb ella, **el teclat de l'app**: mentre una app viu, tot el que s'escriu va a ella i no a la shell.
- **`term`**: `clear`, `reset`, `color`, `move`, `hide_cursor`, `show_cursor`, `width`, `height` i les constants. Si la consola no fa ANSI, no fan res i no donen error.
- **`Promise`**: `p:await()`, `p:and_then(ok[, err])`, `p:catch(err)`, `p:finally(fn)`,
  `p:timeout(ms)`, `p.state`, i `Promise.resolve/reject/is`. Errors `E_NOTASK`, `E_TIMEOUT`,
  `E_INVAL` i `E_BUSY` amb taula `{code, message}`.
- **`net`** (connectar, escanejar, afegir, esborrar, provisioning i `ntp`), **`http.get`** (una
  pàgina amb TLS), **`fs`**, **`qr`**, **`json`**, **`time`**, **`alarm`**, **`term`**, **`app`**,
  **`power`** (les dues menes de son, la velocitat del processador i la bateria), **`led`** (el llum),
  **`imu`** (el sensor del Matrix) i **`button`** (el botó, i el hook `on_button`).

**Encara no hi és**, i una app que ho cridi peta amb «attempt to call a nil value»: `sys.reboot()`,
`console.flush()`, `console.read()`, `console.read_byte()`, `console.on_data()`, `time.utc`/`local`/
`format`/`parse`/`set`/`tz`/`sync`, `Promise.all`/`race`, `app.spawn`/`raise`/`save`/`restore`,
`net.softap`, `power.off`, i els mòduls `store` i `log`. Les taules d'aquest document ho marquen amb
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
| `E_NOTASK` | S'ha cridat `await` fora d'una tasca |
| `E_NOTFOREGROUND` | **Reservat**: cap crida el torna avui. Dins d'una finestra d'alarma el que passa és que `console.is_foreground()` diu `false` i el que s'escriu a la consola queda al log (docs/60) |
| `E_BUDGET` | **Reservat**: el pressupost d'instruccions encara no existeix (docs/20, docs/90) |

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
banc es presta sol, i els treu amb `--no-display` / `--no-touch` (docs/50, docs/85). Els números del
Paper **no són una previsió**: l'1/11 la placa va dir **15 de 18** amb la 0.10.1 (bateria i targeta
dins) i `run air` va donar `air 21.1 C 63.5 %` (docs/95, E-5).

---

## `sys`

`sys` és una taula plana: el que és un valor es llegeix sense cridar res, i el que fa feina és una
funció.

| Nom | Torna | Notes |
|---|---|---|
| `sys.name` | cadena | `"lunokOS"` |
| `sys.version` | cadena | La versió del sistema, la mateixa que respon `version` |
| `sys.board` | cadena | El nom de la placa **tal com surt a `board =` de `platformio.ini`** (`m5stack-atom`), o el nom propi de la placa de la família que canvia què es pot tocar (`m5stack-atom-matrix`). Al PC és el model que el banc ha rebut a `--board` (docs/85) |
| `sys.emulator` | booleà | Cert només al PC: diu que la placa de `sys.board` és de mentida |
| `sys.lua` | cadena | La versió de Lua que porta el sistema |
| `sys.caps()` | taula de capacitats | Vegeu a dalt |
| `sys.heap()` | `{free, used, limit, peak, extra_total, extra_free}` | Bytes del pressupost de l'app i, al darrere, la **memòria extra** de la placa (la PSRAM del Paper): `extra_total` és el que la CPU **pot adreçar** (4 MB, no els 8 del full de dades) i `extra_free` el que en queda; tots dos són **zero** en una placa que no en té, que és la veritat i no un error (docs/75) |
| `sys.gc("step")` | — | Fa un pas de col·lector |
| `sys.gc("full")` | — | Col·lecció completa (cara; no es fa servir enmig d'un dibuix) |
| `sys.gc("count")` | enter | KB en ús |
| `sys.delay(ms)` ⏳ | promesa | Espera sense tocar la cua d'alarmes ni el port sèrie |
| `sys.time_ms()` | enter | Rellotge monòton en mil·lisegons. **Mai salta enrere** |
| `sys.reboot(reason)` | no torna | Permís: cap |

## Saber on s'executa

Una app pot acabar corrent en una placa que no ha vist mai. `sys.board` és l'única cosa que li diu
on és, i és **el nom que la placa té a `platformio.ini`** (`board = m5stack-atom`), no cap àlies
inventat pel sistema. Al PC no hi ha cap placa ni cap línia `board =`, i la manera de no inventar-ne
cap nom és dir quin model fa el banc: `sys.board` respon aquest model (l'ATOM si no es diu res) i
`sys.emulator` diu, a part, que el maquinari de sota és fals.

| `sys.board` | Què hi ha | Què no hi ha |
|---|---|---|
| `m5stack-atom` | consola, LittleFS, NVS, ràdio Wi-Fi amb TLS, provisioning, son, un llum, un botó, un port Grove I2C | pantalla, bateria, teclat, targeta SD, sensor inercial |
| `m5stack-atom-matrix` | el mateix, amb **vint-i-cinc llums** i un **sensor inercial** | el mateix que el Lite |
| `m5stack-paper` | el mateix que el Lite pel que fa al xip (consola, disc, NVS, ràdio amb TLS, provisioning, son), amb **tres tecles**, **targeta SD** al slot (a `/sd`) i sense cap llum | pantalla, bateria, toc i sensor, encara (docs/90) |

Al PC la taula d'un model és la de la placa **amb les peces falses a sobre** (docs/85): el banc té el
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

## `app`

| Crida | Torna | Notes |
|---|---|---|
| `app.id` | cadena | El nom del directori de l'app |
| `app.args` | taula o `nil` | `{path, ext, source}` quan l'app s'obre per obrir un fitxer, o quan el prompt la hi apunta: `run txt res/notes.txt` i `open txt res/notes.txt` són el mateix per a l'app |
| `app.info()` | taula | `{id, title, version, opens}` de l'app: el que el nucli en guarda, **no** el manifest sencer |
| `app.spawn(fn)` ⏳ | promesa | Crea una tasca; la promesa es resol amb el que torni `fn` |
| `app.exit([code])` | no torna | Tanca l'app; `code` per defecte `0`, i la shell diu `app exit N` si no és zero |
| `app.raise()` | — | Demana passar a primer pla (només té sentit dins d'una finestra d'alarma) |
| `app.save(t)` ⏳ | — | Desa una taula petita a `/data/<id>/state.json` |
| `app.restore()` ⏳ | taula o `nil` | Recupera el que va desar `app.save` |

Avui hi ha `app.id`, `app.args`, `app.info()` i `app.exit`; la resta encara no, i cridar-les dona
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

Els dos editors del sistema (`run edit` i `run hexedit`) són exactament això: `term` per dibuixar i
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
| `time.utc([t])` | taula `{year, month, day, hour, min, sec, wday, yday}` | Per defecte, ara |
| `time.local([t])` | taula | Aplica la zona de `cfg` |
| `time.format(t, fmt)` | cadena | `%Y-%m-%d %H:%M:%S` |
| `time.parse(s, fmt)` | enter o `nil, error` | |
| `time.set(epoch)` | — | Permís `rtc_set` |
| `time.tz([cadena])` | cadena | Consulta o fixa la zona (format POSIX) |
| `time.sync()` ⏳ | promesa | NTP; requereix xarxa (avui hi ha `net.ntp`, que és el que fa la feina) |
| `time.caps()` | `{source, synced, wake}` | `source` és `"soc"`, `"rtc"` o `"none"`; `wake` és `"yes"` o `"no"` (si aquesta placa pot armar un despert per a una alarma) |

`time.now()` pot **saltar endavant** quan arriba una sincronització. Per mesurar durades, sempre
`sys.time_ms()`.

**Avui hi ha** `time.now()`, `time.now_ms()` i `time.caps()`. `utc`, `local`, `format`, `parse`,
`set`, `tz` i `sync` encara no existeixen: el rellotge dona l'hora i prou (docs/60).

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
  és una paraula reservada, o sigui que es llegeix `a["repeat"]`. Ho fa així l'exemple
  `apps/alarmqueue/`.
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

Tres exemples més, i cada un ensenya una cosa diferent: `run alarmqueue` (`apps/alarmqueue/`)
obre la **cua del sistema** —quantes n'hi caben, quines hi ha i en quin ordre, cancel·lar-ne una del
mig—; `run timers` (`apps/timers/`) arma tres temporitzadors i demostra que l'app suspesa no
atura el nucli; i `run alarmset` (`apps/alarmset/`) demana per consola quants segons falten i
arma una alarma **persistent i amb `raise`**, que és la que fa que el sistema apagui la placa i la
torni a encendre amb el rellotge (docs/60). I `run later` (`apps/later/`) és el cas complet:
arma una alarma, **surt amb `app.exit()`** i torna per la finestra d'alarma, on diu quants
mil·lisegons s'ha desviat.

**`persist` sí que hi és**: una alarma persistent es desa a la NVS amb l'**època a dins** (el
monòton torna a començar a cada arrencada), i en arrencar les vençudes es disparen amb el seu
`late_ms`. Sense època no s'escriu res, perquè una hora relativa no vol dir res després d'un
reinici (docs/60).

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
| `net.ntp([servidor[, ms]])` | **promesa** | Posa el rellotge amb un servidor de temps (`pool.ntp.org` per defecte) i es resol `true` quan el rellotge ja hi és. **No** deixa la ràdio encesa: la resposta és una hora, no una connexió |
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

L'exemple de mà és `run page` (`apps/page/`), que baixa una pàgina i n'ensenya l'estat i el cos.

## `fs`

| Crida | Torna | Notes |
|---|---|---|
| `fs.mounts()` | taula | `{path, type, writable}` de cada muntatge, sense l'espai (vegeu `fs.space`). L'ordre és el de la taula del nucli i n'hi caben **deu** |
| `fs.space([path])` | taula o `nil, error` | `{total, free}` del muntatge on viu el camí; `E_UNSUPPORTED` si el backend no ho sap |
| `fs.list(path)` | taula | `{name, type, size, mtime}` de cada entrada |
| `fs.stat(path)` | taula o `nil, error` | `{type, size, mtime}` |
| `fs.mkdir(path)` / `fs.remove(path)` / `fs.rename(a, b)` | — | El `E_PERM` de la taula d'errors és **reservat**: el model de permisos no existeix encara, i avui escriure el decideix el terreny de l'app, no el manifest |
| `fs.open(path[, mode])` | objecte fitxer | `mode`: `"r"`, `"w"`, `"a"`, `"r+"` |
| `fs.read(path[, max])` | cadena o `nil, error` | El fitxer sencer, o com a molt `max` bytes |
| `fs.read_at(path, offset, len)` | cadena o `nil, error` | Accés aleatori sense obrir |
| `fs.write(path, data)` | enters | Escriu sencers (temporal + reanomenar) |
| `fs.data([sub])` | cadena | `/data/<id>/` o un camí a dins seu |
| `fs.cfg([unitat])` | taula o `nil, error` | `{unit, dir}`: on van els ajustos d'aquesta app. Amb `"auto"`, `"sd"` o `"littlefs"` els mou (docs/70) |

**Avui hi ha** `fs.open`, `fs.list`, `fs.stat`, `fs.read`, `fs.read_at`, `fs.write`, `fs.mkdir`,
`fs.remove`, `fs.rename`, `fs.data`, `fs.mounts`, `fs.space` i `fs.cfg`.

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
- **Per veure-ho de mà**: `run mtab` llista els muntatges i l'espai de cadascun.
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
  `glow` (`run glow`), que juga els quatre efectes i marxa amb un fade.


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
- L'exemple és l'app `air` (`run air`), que la llegeix cada dos segons.

## `ui`

La vista de l'app: **el seu propi canvas**, i el moment en què demana al sistema de posar-lo al vidre.
El vidre té un sol buffer i és del sistema; l'app dibuixa al seu, i `ui.present` és l'única cosa que el
fa arribar a la pantalla. **El sistema no dibuixa mai la pàgina d'una app** —ni splash, ni netejar quan
una app marxa—: cada app decideix **què** es dibuixa, amb **quina qualitat** i **quan** (docs/50).

L'únic que el sistema pinta són **les 48 files de dalt**: la barra amb l'hora, el títol de l'app i la
càrrega, que no és de cap app (docs/50, «La barra del sistema»). Per això la vista d'una app comença a
la fila 48 i `ui.caps().y` ho diu.

| Crida | Torna | Notes |
|---|---|---|
| `ui.caps()` | `{width, height, levels, x, y}` | La grandària de la **vista**, els seus nivells (16) i **on està** dins del vidre. L'alçada és **el que queda sobre el teclat** mentre la banda és al vidre (0.84.0) |
| `ui.clear(level, [mode])` | `true` o `nil, error` | Tota la vista d'un nivell |
| `ui.rect(x, y, w, h, [level], [mode])` | `true` o `nil, error` | Un rectangle; `level` per defecte 0 (negre) |
| `ui.pixel(x, y, [level], [mode])` | `true` o `nil, error` | Un píxel |
| `ui.present([mode])` | `true` o `nil, error` | **El sistema compon** el que ha canviat al buffer del vidre i demana el pintat. Sense `mode`, cada regió es pinta amb **la qualitat amb què es va dibuixar**; amb `mode`, tot el que està pendent es pinta amb aquesta |
| `ui.damage()` | número | Quantes regions esperen un `present` |
| `ui.title(text)` | `true` o `nil, error` | El títol que surt **a la barra del sistema** |
| `ui.rotation([degrees])` | el gir actual | **Gira la pàgina sencera**: 0, 90, 180 o 270. El vidre, la barra i els dits giren amb ella, i l'app rep `on_resize` amb la mida nova |
| `ui.idle()` | booleà | Si el pintat anterior ha acabat. **No diu que sí abans que el motor hagi tingut temps de marcar-se ocupat**, o sigui que es pot preguntar de seguida després del `present` |
| `ui.resize(w, h)` | `true` o `nil, error` | Una vista d'una altra mida: el sistema en fa una altra, blanca, i crida `on_resize` |
| `ui.canvas(w, h)` | **un objecte** o `nil, error` | Un control **amb vista pròpia**: un buffer i un lloc, que el sistema compon amb la resta quan l'app fa `ui.present` (vegeu a sota) |
| `ui.control(w, h)` | **un objecte** o `nil, error` | Un control **sense vista**: un marc amb fills que **no costa cap píxel** i que pinta al canvas que el conté, tallat al seu marc. Una graella, una caixa o un espaiador són això; amb `focusable = true` també entra al recorregut del dial i del tàctil, amb `on_activate` com a clic |
| `ui.label(w, h)` | **un objecte** o `nil, error` | Un control **sense vista** que és una línia de text: fons, color, cara, alineació i retall amb `...` (vegeu a sota) |
| `ui.button(w, h)` | **un objecte** o `nil, error` | Un control **sense vista** amb fons i vora que pot tenir un fill; el fill es posa amb `button:add` i es talla al marc del botó. **És enfocable i clicable** amb el dial i amb el tàctil (vegeu a sota) |
| `ui.checkbox(w, h)` | **un objecte** o `nil, error` | Un control **sense vista** que és una fila: una **caixa quadrada amb un tick** i el lloc de les paraules, que són un fill. **És enfocable i clicable**, i el clic **gira l'estat** i avisa amb `on_change` (vegeu a sota) |
| `ui.progress(w, h)` | **un objecte** o `nil, error` | Una **barra**: fons, pista i el tros fet, tot del sistema. `value` és 0 a 100 i **un negatiu és «encara no ho sé»**. **No és enfocable** (3/11, 0.69.0) |
| `ui.slider(w, h)` | **un objecte** o `nil, error` | **Un número que mou el dial**: la pista, el botó i el número amb la seva unitat (`suffix`). **És enfocable**: les fletxes canvien `value` de `step` en `step` i l'app ho sap per `on_change(self, value)` (3/11, 0.69.0) |
| `ui.row(w, h)` | **un objecte** o `nil, error` | **Una fila de llista**: `title` a l'esquerra, `detail` a la dreta, `subtitle` a sota i el filet del separador. `marked` la inverteix. **No és enfocable** (3/11, 0.69.0) |
| `ui.panel(w, h, [flow])` | **un objecte** o `nil, error` | **El lloc de la pàgina**: `'vertical'` (per defecte), `'horizontal'` o `'overlay'`, amb `padding` i `gap`. Els fills es posen amb `panel:add(child)` i la part de lloc lliure que vol cada un amb `child.weight` (3/11, 0.70.0) |
| `ui.tabs(w, h)` | **un objecte** o `nil, error` | Una **tira de pestanyes**: `items` (un array de paraules, vuit com a molt) i `index` (1 a n, i 0 = cap). **És enfocable**: les fletxes canvien la triada i `on_change(self, index)` avisa (3/11, 0.70.0) |
| `ui.dots(w, h)` | **un objecte** o `nil, error` | **Els punts de pàgina**: `pages` i `current`, amb la gran més gran. **No és enfocable**, i amb una sola pàgina no dibuixa res (3/11, 0.70.0) |
| `ui.list(w, h)` | **un objecte** o `nil, error` | **Files en pàgines i mai scroll**: `items` (una llista de taules amb `title`, `detail` i `subtitle`, o de cadenes, que són el títol), `row_height`, `page` i `marked` (la fila triada de tota la llista, 1 a n, o 0). El peu diu `2 / 4` i **les fletxes del dial canvien la pàgina** amb `on_page(self, page)` (3/11, 0.71.0) |
| `ui.textbox(w, h)` | **un objecte** o `nil, error` | **Una línia on es tecleja**, amb el cursor de bloc: `text`, `cursor` (quants bytes hi ha abans del cursor), `caret` (si el bloc es pinta; el parpelleig és de l'app), `mask` (puntets en lloc de lletres), `color`, `scale`. **És enfocable** (3/11, 0.72.0) |
| `ui.keyboard()` | **un objecte del sistema** | **El teclat, que no és un control**: `kb.target = box` (on escriu), `kb:show('alpha'\|'numeric')`, `kb:hide()`, `kb:height()` (la banda que pren) i `kb:visible()`. El teclat **pinta el sistema**, no l'app; des de 0.83.0 és un QWERTY de minúscules amb majúscules, barra d'espai i icones, i la banda fa quatre files en els dos jocs. **Des de 0.84.0 la vista de l'app és el que queda sobre la banda** (`ui.caps().height` baixa a 736 i l'app rep `on_resize`) (3/11, 0.72.0) |
| `ui.dialog(w, h)` | **un objecte** o `nil, error` | **Una targeta sobre la pàgina**: `title`, `text` (que **s'ajusta** en línies i no es talla), `note`, `accept` i `cancel`, `emphasis` (la porta que s'inverteix: 0 cap, 1 el cancel·lar, 2 l'acceptar) i `choice` (la porta on és el dial). El dit tria la porta que toca; el centre prem `on_accept`/`on_cancel` (3/11, 0.73.0) |
| `ui.sparkline(w, h)` | **un objecte** o `nil, error` | **La forma de les últimes mostres**: una barra per mostra (la més vella a l'esquerra), l'alçada contra **el pic de la finestra**, i `top` per a una segona sèrie que és part del mateix total (es dibuixa buida). No és enfocable (3/11, 0.74.0) |
| `ui.pie(w, h)` | **un objecte** o `nil, error` | Un **pastís de quatre porcions com a molt**: `slices` (valors; els angles surten de la suma) i el to més fosc; els altres tres són quatre passos més enllà, que és com l'app pot dibuixar-ne la llegenda (3/11, 0.74.0) |
| `ui.month(w, h)` | **un objecte** o `nil, error` | **Un mes de calendari**: `year`, `month` (1 a 12), `selected` i `today` (un dia del mes, o 0), `weekdays` (set noms curts, que són cosa de l'app), `color`, `dim` i `scale`. Set columnes i **sis setmanes sempre**; el triat s'inverteix i el d'avui porta marc. Les fletxes caminen els dies i el centre tria amb `on_pick(self, day)` (3/11, 0.75.0) |
| `ui.menu(w, h)` | **un objecte** o `nil, error` | **El menú contextual**: una targeta d'opcions sobre la pàgina. `items` (un array de paraules, o de taules amb `label`, `tag`, `hidden` i `primary`, dotze com a molt), `index` (l'opció on és el dial, 1 a n), `title`, `color`, `dim` i `scale`. **Una opció que l'app no pot fer ara s'amaga** (`hidden`) i no es mostra morta; la de `primary` s'inverteix. Les fletxes salten les amagades i el centre, o un dit, tria amb `on_pick(self, index)` (3/11, 0.76.0) |
| `ui.menu_bar(w, h)` | **un objecte** o `nil, error` | **Una barra de menús**: `items` (fins a cinc títols; cada un `{title=..., items={...}}`), `open` (el títol desplegat, 1 a n, o 0), `index` (l'entrada on és el dial), `bar_height`, `color`, `dim` i `scale`. Una entrada és una paraula, `{label=, checked=, enabled=}` o `'-'` per a una línia. **Una elecció crida `on_pick(self, title, entry)` i el sistema plega el menú** (3/11, 0.77.0) |
| `ui.textbox_multiline(w, h)` | **un objecte** o `nil, error` | **Un camp de diverses línies**: `text`, `cursor` (bytes abans del cursor), `caret`, `wrap` (si les línies llargues es parteixen), `color`, `background`, `border` i `scale`, i **`box:rows()`** diu quantes files s'hi veuen. **Retorn escriu un salt de línia** i el dit posa el cursor on toca (3/11, 0.78.0) |
| `ui.selector(w, h)` | **un objecte** o `nil, error` | **Un camp que s'escull i no s'escriu**: `label` (la pregunta, en el to `dim` i petit), `items` (les opcions), `index` (**la triada, 1 a n, i 0 = cap**, que és un estat de debò), `empty` (com es diu «cap» en la llengua de l'app) i `choosing` (si la llista és a sobre, que inverteix només la columna de la fletxa). **La llista no és al control**: el dit o el centre disparen `on_open(self)` i l'app hi posa un `ui.menu` (3/11, 0.79.0) |
| `ui.notification_bar(w, h)` | **un objecte** o `nil, error` | **La banda dels avisos**: `items` (cada avís és una paraula o `{title=, text=}`, **el més nou al final**), `color`, `dim`, `background`, `border` i `scale`. Les targetes s'apilen cap amunt amb **la més nova a baix**, tres com a molt; el que no hi cap **no es perd**: es compta amb un `+N`. La creu dispara `on_close(self, item)` i la targeta `on_pick(self, item)` (3/11, 0.80.0) |
| `ui.icon_grid(w, h)` | **un objecte** o `nil, error` | **Una graella de cel·les amb pàgines**: `items` (una paraula, o `{label=, mark=}`, on `mark` és la paraula curta de la caixa de la icona), `index` (la cel·la on és el dial, 1 a n, o 0), `marked` (la triada, que s'inverteix), `columns` (0 = el control decideix per l'amplada), `color`, `dim` i `scale`. **La pàgina segueix el cursor** i les fletxes caminen les cel·les; el centre i el dit trien amb `on_pick(self, index)` (3/11, 0.81.0) |
| `ui.frame(x, y, w, h, [level], [mode])` | `true` o `nil, error` | El **voltant** d'una caixa, d'un píxel, i el mig sense tocar |
| `ui.line(x0, y0, x1, y1, [level], [mode])` | `true` o `nil, error` | De punt a punt |
| `ui.circle(cx, cy, r, [level], [mode])` | `true` o `nil, error` | La **línia** del cercle |
| `ui.disc(cx, cy, r, [level], [mode])` | `true` o `nil, error` | El cercle **ple** |
| `ui.text(x, y, text, [scale], [level], [mode])` | `true` o `nil, error` | Un text amb la cara del canvas i, si no en té, amb la font del sistema, `scale` vegades la seva mida (1 a 16) |
| `ui.image(x, y, path, [w], [h], [mode])` | `true` o `nil, error` | **Una imatge d'un fitxer**: sense `w` ni `h` es dibuixa a la mida del fitxer; amb una mida, s'escala al més proper |
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
  treu un 120 ms que és seu i no de la tinta (és el que va passar amb `run panel` fins a la 0.19.2, i
  està escrit a `docs/95`, E-7). El motor del controlador no es marca ocupat en el mateix instant que rep
  la comanda, i per això el sistema no respon «ha acabat» fins passats uns mil·lisegons: és una guarda
  del conductor i no una mesura (`docs/50`).
- **Les imatges són fitxers i no taules de píxels a la memòria.** `ui.image` llegeix la capçalera, baixa les
  files una a una i les deixa al canvas, i el que queda al heap quan torna és el que hi havia abans. Els
  formats que es llegeixen avui són **PNG** (tipus de color 0, 2, 3, 4 i 6, profunditats 1 a 16, tots els
  filtres), **JPEG** (base i seqüencial estès, amb qualsevol factor de mostreig i marcadors de reinici),
  **BMP** (1, 4, 8, 16, 24 i 32 bits, amb paleta o sense, de dalt o de baix), **GIF** (87a i 89a, la primera
  imatge, LZW, transparència i entrellaçat), **ICO** (una imatge de BMP o un PNG a dins) i la família
  **netpbm** (`PBM`, `PGM` i `PPM`, en text i en cru). El que no es llegeix es diu amb un codi i no es dibuixa
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
- **Cada dibuix guarda la seva regió i la seva qualitat.** `ui.rect(0, 40, 60, 120, 7, 'gc16')`
  apunta «del (0,40) al (60,160), i que quedi bé»; `ui.rect(0, 220, 40, 40, 0, 'du')` apunta «aquest
  quadre, i ràpid». `ui.present()` envia cada regió amb la seva i, com que el controlador només pot
  pintar una cosa alhora, la segona espera que la primera acabi (docs/50).
- **El que es pot ajuntar s'ajunta.** Dues regions de la mateixa qualitat que es toquen (a dos píxels)
  es pinten d'un sol cop, i dues tirades de text en són una. Si una app dibuixa més de vuit regions
  diferents abans d'un `present`, totes es tornen una de sola pintada amb `gc16`, que és la qualitat que
  no pot quedar malament, i aquesta regió inclou la darrera que s'ha dibuixat; `ui.damage()` ho diu abans
  de pagar-ho.
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
  per tant res del que dibuixa no canvia: `run font-bdf` i `run font-pcf` ensenyen la cara del fitxer al
  costat de la del sistema, i les dues surten del **mateix font** (el `.pcf` de l'app està fet amb
  `bdftopcf` del `.bdf` de l'altra, i la prova comprova que el bloc és idèntic byte a byte). Un fitxer que
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
  del fitxer). **Hi ha dues apps i no fan el mateix**: `run font-ttf` dibuixa el `.ttf` d'aquest repositori
  —les mateixes sis lletres de `font-bdf` i `font-pcf`, a dues, quatre i sis mides—, que és el que prova que
  el lector llegeix el que han de llegir els altres dos; i `run fonts-ttf` dibuixa **fonts de debò**: DejaVu
  Sans, Serif i Sans Mono, retallades a l'ASCII imprimible amb `pyftsubset --unicodes=U+0020-007E` (8 084 a
  10 896 B cada una) i amb la seva llicència al costat (`/res/LICENCE.txt`). Són tres perquè **tres és el que
  el sistema guarda alhora**, i la quarta —la negreta— hi entra quan l'app torna el mono: `release()` és el
  que fa lloc.
- **Quan el sistema canvia la mida de la vista** —una rotació del vidre, un dia, o la banda del teclat que hi
  entra i en surt (0.84.0)— l'app rep `on_resize(width, height)`. El sistema no sap què hi va: la feina de tornar a
  dibuixar és de l'app, i `cv:resize(w, h)` és el que necessita un `panel` per prendre la mida nova. Una app sense
  `on_resize` és una app legal; simplement no dibuixa mai la vista nova.

```lua
local view = ui.caps()
ui.clear(15, 'gc16')                    -- la pàgina, i que quedi neta
ui.rect(0, 200, view.width, 40, 0, 'du')   -- un avís que es mou sovint, i ràpid
ui.present()                            -- dues regions: dos pintats, un per qualitat
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
- L'exemple és l'app `panel` (`run panel`), que dibuixa una pàgina de prova a la seva vista i espera que
  el vidre acabi.

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
  l'arrel (docs/70), una fila de la taula de muntatges, i una app **la llegeix i no hi escriu**, com `/apps`.
  El que hi ha avui: `dejavusans.ttf` (9 948 B), `dejavusans-bold.ttf` (8 548 B), `dejavusansmono.ttf`
  (8 084 B) i `dejavuserif.ttf` (10 896 B) —retallats a l'ASCII imprimible amb `pyftsubset`, i la llicència
  de DejaVu al costat, `/fonts/LICENCE.txt`—. `run fonts-ttf` i `run label` les fan servir i no en guarden
  cap còpia.

- **Una cara pot ser parcial**: un codi que no hi és es dibuixa com un forat i la línia avança igual, o sigui
  que una cara de deu xifres és una cara legal i el text del voltant no es desplaça.
- **Qui converteix la cara en píxels és el nucli, i ningú més.** La cadena sencera és
  `cv:text`/`ui.text` → `luabind` (`text_at`) → `ui::canvas_text` (`lib/kernel/src/ui.cpp`) → `draw_text` →
  `font::runs(face_of(canvas.face), …)`, que camina els glifs i deixa cada tros com un rectangle al buffer
  del canvas. **El mapa de bits d'un tipus de lletra no surt mai del nucli**: a Lua arriben un número de mà
  (`ui.load_font`), tres números que descriuen la cara (`font:size`) i una amplada (`font:text_width`). Una
  app no processa cap font, i si ho intentés no tindria amb què: l'objecte `font` no té cap camp de glifs ni
  cap crida que els torni.
- **La memòria és de l'app**: el bloc de glifs (`96 × 16 × 2 = 3 072` bytes) compta al pressupost
  (docs/75) i torna quan l'app crida `font:release()` o quan l'app s'acaba; un canvas que la feia servir
  torna a la font del sistema en comptes de llegir un bloc que ja no hi és.
- **Dues apps per mirar-ho**: `run fonts` (la font del sistema i una cara carregada a la mateixa pàgina, amb
  els números de cada cara a la consola) i `run sheet` (tots els glifs del sistema amb la seva cel·la).

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
| `cv:clear([level], [mode])` | `true` o `nil, error` | Tot el canvas |
| `cv:place(x, y)` | `true` o `nil, error` | On seu dins del pare, en píxels del pare |
| `cv:resize(w, h)` | `true` o `nil, error` | **Una altra mida** per a un control que no té píxels propis (un panel, una etiqueta, una caixa de text): la mesura del grup; el que la necessita és un `on_resize` que torna a col·locar la pàgina (0.84.0). Un canvas **amb buffer** la refusa: els seus píxels són un bloc demanat amb una mida, i una altra mida és un altre canvas |
| `cv:rect`, `cv:frame`, `cv:line`, `cv:circle`, `cv:disc`, `cv:text` | `true` o `nil, error` | Les mateixes primitives que la vista, dibuixades al canvas propi |
| `cv:add(fill)` | `true` o `nil, error` | El fill passa a estar **dins** d'aquest canvas, i és l'últim (la capa del davant). Un canvas té **un sol pare** |
| `cv:draw()` | `true` si ha dibuixat, `false` si no hi havia res | **Es dibuixa ell i després pregunta als fills**, en l'ordre en què es van afegir. **Si no té cap canvi, no fa res** |
| `cv:invalidate([x, y, w, h])` | `true` o `nil, error` | «Ha canviat el que ensenyo». Amb una caixa, **això** és el que s'ha de pintar; sense, tot el canvas |
| `ui.root()` | **l'objecte de la vista** | El mateix cada vegada: la vista també és un control, amb els seus `add`, `draw` i fills |
| `cv:parent()` | el pare, o `nil` | El canvas que el conté; `nil` vol dir que és la vista |
| `cv.on_draw` | el camp que posa l'app | `function(self)` que pinta el canvas propi; opcional |
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
  o la llista de vuit s'omple i tot el canvas es pinta d'un sol cop amb `gc16` (docs/50). Una línia recta
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
- **Dos sostres i no un**: hi ha fins a **32 controls**, i només **vuit** poden tenir vista pròpia (la de
  l'app inclosa). Un control sense vista no gasta ni un byte de píxel; si no hi ha lloc, `ui.canvas` o
  `ui.control` responen `nil` amb `E_NOMEM`.
- **Un control sense vista no és cap forat a l'arbre**: els seus fills es componen igual, amb el
  desplaçament de cada capa sumat, i el que ell dibuixa cau al canvas que el conté (el primer que en té) i
  **es talla al seu marc**: no pot pintar fora de la seva caixa. Un `UILabel` no necessita cap buffer.
- **Una vista feta de nou se'ls emporta**: si el sistema torna a fer la vista (una rotació, un `ui.resize`),
  els canvas de dins moren, i un objecte que l'app encara tingui **respon `nil, E_INVAL`** en lloc
  d'escriure al canvas que hagi ocupat el seu lloc.
- **Moure un canvas sí que torna a pintar el que deixa enrere**: el pare (o el primer control que pinta,
  si el pare no té buffer) rep exactament el rectangle que el fill ocupava, i el fill es compon al lloc nou.
- L'exemple és l'app `layers` (`run layers`), que fa dos canvas, els col·loca i els compon amb un sol
  `ui.present`.

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
  `ui.load_font`; el label la fa servir per mesurar i per dibuixar.
- **Si el text no hi cap, s'acaba amb `...`**: es queda el prefix més llarg que, amb els tres punts, cap a
  l'amplada del marc; si ni els punts hi caben, els punts són el que es dibuixa i el marc els talla.
- **Només es redibuixa quan cal**: el label neix amb el senyal de dibuixar-se, i després només quan canvia el
  text —o el color, el fons, l'alineació, la mida o la cara— i només si el valor és diferent. Un
  `ui.present` sense canvis no crida el seu `on_draw`. Quan la vista es torna a fer (una rotació o un
  `ui.resize`), l'app rep `on_resize` i torna a fer els labels, que es dibuixen una vegada.
- **El fons és opac a posta**: el control sense vista escriu al canvas que el conté i el seu marc no té
  transparència; un label sobre un panell fosc vol `label.background` del nivell del panell (docs/90).
- **Es pinta amb `gc16`**, la qualitat que no deixa el fantasma del mot anterior: un label és una pàgina de
  text en miniatura, i canviar-ne el text ha de deixar el text nou net (docs/50, docs/90).
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

L'app `label` (`run label`) és una pàgina de trenta labels: les tres cares carregades i la del sistema,
quatre tamanys, les dues alineacions, dos retalls amb `...`, els setze nivells i els comptes de redibuix:
un present sense canvis no dibuixa res, i canviar el text o el color en dibuixa un.

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
| `button.pressed` | `true` o `false` (fals per defecte) | Mentre és cert, **el relleu s'inverteix** i el botó sembla enfonsat |
| `button.focused` | `true` o `false` (fals per defecte) | **L'estat del focus**, que porta el sistema: mentre és cert, el marc pinta **una pausa de dos píxels i una banda de set** —el doble marc del control seleccionat—, negra sobre fons clar i blanca sobre fons fosc |
| `button.focusable` | `true` o `false` (cert per defecte) | Si el dial i el tàctil poden aturar-s'hi. Un botó amb `false` continua pintant-se i pot rebre clics de l'app, però no entra al recorregut del sistema |

| Callback | Què fa |
|---|---|
| `button.on_click(self)` | El clic del centre o d'un dit a sobre. Si existeix, és el que es crida |
| `button.on_activate(self)` | El mateix clic quan no hi ha `on_click`; és el nom genèric per a un control que no és un botó |
| `button.on_focus(self, focused)` | Quan el dial o el tàctil mouen el focus: arriba (`true`) o se'n va (`false`). El primer control d'una pàgina ja neix enfocat, i per això el seu `on_focus` no es crida en crear-lo |

- **Una cara de bits és de setze per setze, i una taula es pot fer d'una font de debò** (2/11, 0.55.0). La cel·la
  era de vuit per vuit —el que demanava una 5 × 7 doblada— i ara és de **setze per setze amb dues bytes per
  fila** (`kGlyphRowBytes`), perquè setze columnes no caben en un byte: el bloc d'una cara passa de 768 a
  **3 072 B** i el que no canvia és el camí que el dibuixa. I per tenir una lletra gran, arrodonida i amb
  majúscules i minúscules sense pagar un rasteritzador a cada pàgina, hi ha **`tools/ttf_to_lfont.py`**:
  converteix un `.ttf` en un `.lfont` a una mida triada (Pillow, el glif centrat a la cel·la perquè la taula és
  d'amplada fixa, el traç opcional) i refusa el que no cap a setze. `run fonts` carrega el
  `digits-8x8.lfont` que viatja amb l'app i en diu la mida i l'amplada de la línia, i `run font-mix` fa el
  mateix amb un `abc-6x7.lfont`. El preu de la taula fixa: cada glif ocupa tota la cel·la, o sigui que un
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
  el focus. I com que el canvi de focus és un canvi d'estat de la pàgina, l'app l'ha de **rentar amb `gc16`**
  al torn següent: un `du` hi deixa l'anell anterior a un quart de la tinta, que és un anell gris que ningú no
  ha demanat (docs/50, docs/90).

- **La pàgina dels botons ja no porta una taula de bits** (3/11, 0.56.2): `run buttons` carrega **Lato
  Black** d'un `.ttf` d'outlines retallat a l'ascii imprimible (`pyftsubset --unicodes=U+0020-007E`,
  **19 340 B**, la llicència OFL al costat, `/res/LICENCE-lato.txt`) i dibuixa els vuit labels a **24 px**
  (`scale = 3`), el títol a 32 i les notes a 16, amb el mateix tipus de lletra. És una **rodona** de debò —el
  `g` té noranta-un punts i és el glif que va fer falta tallar amb menys trossos de corba— i una cara
  d'outlines és **proporcional**: les amplades surten de la taula del fitxer i el text no té la respiració de
  la cel·la fixa; el preu és que el nucli ha de dibuixar cada glif, i per això les dues cares conviuen
  (docs/85).

- **El botó té forma i volum, i el focus no es pot perdre de vista** (2/11, 0.51.0): el marc és una **caixa
  amb els cantons rodons** (dotze píxels, i mai més d'un terç del costat més petit — un botó que és tot cantó
  és una pastilla i no un botó), amb una vora de dos píxels —tres en un botó de pàgina des de la 0.56.2—. A
  dins hi ha el **relleu**: una aresta clara a dalt i a l'esquerra i una ombra a baix i a la dreta (`fons + 4`
  i `fons − 4`, o sigui que un botó blanc té ombra grisa i un de negre té lluentor grisa), i **mentre es prem
  les dues canvien de lloc**, que és tota la diferència entre un botó que surt i un que s'enfonsa. El **focus
  és un anell** cinc passes endins, en el nivell que no es pot confondre: negre sobre un botó clar i blanc
  sobre un de fosc. És un píxel de tinta i no un degradat, perquè el panell té setze nivells i el que es veu
  és una aresta. El gruix de la vora i el de l'anell són els de la 0.56.2, més amunt.
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

L'app `buttons` (`run buttons`) posa vuit labels de la mateixa cara —Lato Black, la del títol i les
notes— a **tres tamanys** (16, 24 i 32 px) dins de **vuit botons blancs**. El vuitè es prem i es deixa anar amb
un `gc16` que neteja el forat, el focus es renta quan es mou, i els comptes de la consola mostren que un botó
que canvia també torna a dibuixar el seu fill.

### `UICheckbox`: una caixa, un tick i l'estat

**Un checkbox és una fila** (`ui.checkbox(w, h)`): una **caixa quadrada** a l'esquerra i, a la dreta, el lloc de
les paraules que diuen què es marca. Les paraules són un `UILabel` fill que es posa amb `cb:add(etiqueta)`,
exactament com el text d'un botó, i es col·loca amb **`cb:box()`** —els píxels que ocupa la caixa, que són del
nucli i no un camp—. El marc del control és la fila i el que dibuixa és la caixa que hi ha a dins (docs/30,
docs/90).

| Camp | Valor | Què fa |
|---|---|---|
| `checkbox.background` | 0–15 (15 per defecte) | El nivell de la fila, que és **opaca**: les paraules s'hi posen a sobre |
| `checkbox.border` | 0–15 (0 per defecte) | El nivell de la vora de la caixa |
| `checkbox.mark` | 0–15 (0 per defecte) | El nivell del **tick**, que és un color propi perquè un tick negre en una caixa negra és un checkbox que no diu res |
| `checkbox.checked` | `true` o `false` (fals per defecte) | **L'estat**: l'app el llegeix i l'escriu, i el clic del sistema el gira |
| `checkbox.pressed` | `true` o `false` | Mentre és cert la caixa és un forat: el cos baixa un pas i el relleu s'inverteix |
| `checkbox.focused` | `true` o `false` | L'estat del focus, que porta el sistema: l'anell es pinta **fora de la caixa** |
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
- **La caixa és del nucli i es mesura amb `cb:box()`.** El marge de la fila és de quatre píxels a dalt i a baix
  —el lloc que necessita l'anell del focus—, la vora té el pes que el botó ensenya (tres píxels en una caixa de
  pàgina, dos en una de petita i un en un xip) i el tick és **dues pinzellades** d'un gruix que creix amb la
  caixa. Una fila massa curta per a quatre píxels de marge en gasta dos, i una de menys de sis no té caixa.
- **L'anell del focus va fora de la caixa**, dues píxels de banda i un de pausa abans de la vora: una caixa de
  dotze píxels amb vora de dos no té lloc per al doble marc que el botó porta a dins, i un anell dibuixat a dins
  s'empassaria el tick. La fila és de l'app, i una app que vulgui que les paraules diguin el focus les pot
  pintar al seu `on_focus` (docs/30, docs/90).
- **La fila és opaca, com el marc d'un botó**: les paraules es componen després del checkbox i queden tapades
  pel fons nou, o sigui que un checkbox que es redibuixa **invalida els seus fills** i el present els torna a
  pintar (docs/30, docs/90).
- **Es pinta amb `du`**, i el canvi el renta l'app amb un `gc16` al present següent: un tick és poca tinta, però
  un anell que s'ha mogut deixa el fantasma de l'anell anterior (docs/50, docs/90).
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
ui.present('gc16')
```

L'app `checkbox` (`run checkbox`) és una pàgina de cinc files: **les paraules dins de cada fila**, una fila que
**neix marcada**, una que el dial **no pot tocar** (`focusable = false`, i el recorregut del sistema la salta),
una de fosca amb el tick i la vora blancs, i una línia que compta quantes estan enceses. Cada canvi diu a la
consola el nom i l'estat nou, i la pàgina es renta amb `gc16` perquè l'anell que s'ha mogut no hi deixi el
rastre.

### `ui.progress`, `ui.slider` i `ui.row`: el que dibuixa el sistema (0.69.0)

**Cap control no es dibuixa en Lua.** Una app diu els camps, col·loca el control i decideix quan la pàgina va al
vidre; cada píxel —la pista, el botó, el número, el tick, l'anell del focus— el posa
`lib/kernel/src/ui.cpp`. És el que fa que dos apps tinguin els mateixos controls, i el que permet que una pàgina
de controls costi un repintat i no un per control (docs/30, docs/90).

| Control | Camps | Enfocable |
|---|---|---|
| `ui.progress(w, h)` | `value` (0 a 100, o **−1 = encara no ho sé**), `background`, `border`, `track` (la pista), `fill` | **no**: una barra no és una cosa que es prem |
| `ui.slider(w, h)` | `value`, `min`, `max`, `step`, `suffix` (el `%`), `background`, `border`, `track`, `fill`, `text`, `scale` | **sí**: les fletxes mouen el número i `on_change(self, value)` avisa, amb el camp ja escrit |
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
ui.present('gc16')
```

`run controls` és una pàgina amb els tres: una barra a mig fer, un slider que el dial mou, i quatre files de
llista amb la tercera marcada. Les proves que mesuren la conseqüència són `test_a_progress_bar_fills_a_prefix_of_its_track`,
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
- **`child.weight` parteix el lloc que sobra**: els fills amb pes 0 es queden la mida que tenen, i el que queda es
  reparteix pel pes amb **el darrer que pren el romanent** (o cent píxels d'arrodoniment deixarien una tira de pàgina
  al fons del panel). Un pes de 0 a 15.
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
ui.present('gc16')
```

### `ui.tabs` i `ui.dots`: on ets a la pàgina (0.70.0)

- **`ui.tabs`**: `items` és un array de paraules (vuit com a molt) i `index` és quina està triada, 1 a n; **0 vol dir
  cap**. Les columnes són iguals, i **la triada és la que no té la línia a sota**: a dos tons no hi ha negreta ni
  color, i treure la línia és el que diu «ets aquí» (el que el vell va aprendre a `tab_strip.h:44`). Les fletxes del
  dial la mouen mentre té el focus i l'app ho sap per `on_change(self, index)`.
- **`ui.dots`**: `pages` i `current`, i la del `current` més gran. **Amb una sola pàgina no dibuixa res** —un punt sol
  no diu res i costaria un repintat del panel per dir-ho— i el lloc de cada punt és el del més gran, o la tira es
  mouria de costat quan la gran canvia de punta.
- Cap dels dos no és un dibuix de l'app: són controls del nucli, com la resta (docs/92 §10).

### `ui.list`: files en pàgines, i mai scroll (0.71.0)
**A la tinta no es fa scroll**: un scroll és un centenar de pintats petits i una pàgina és un de sol. Per això una
llista **pagina**, i el peu diu en quina pàgina ets.

- **`items` és una taula de taules** (`{title=..., detail=..., subtitle=...}`) o **de cadenes**, que són el títol.
  Les files es pinten amb el mateix pintor que `ui.row` —un sol pintor, o la llista i la fila divergiran— i la
  fila `marked` s'inverteix.
- **Quantes files hi caben i quantes pàgines surten és una sola aritmètica**, i la pregunta la contesta el nucli: el
  peu només existeix si hi ha més d'una pàgina i, quan hi és, es queda la seva banda i les files es compten **de nou**
  en el que queda. Una llista que digués «2 / 4» sobre les files de la cinquena pàgina seria una llista que menteix.
- **`marked` és una fila de tota la llista** i el control en pinta una pàgina: una fila d'una altra pàgina no és la
  triada d'aquesta, i el control ho sap (zero).
- **Les fletxes del dial canvien la pàgina** mentre la llista té el focus i l'app ho sap per `on_page(self, page)`.
  El peu només ensenya el galó del costat on hi ha alguna cosa, i al final del recorregut la tecla torna al dial.
- **No hi ha cap `visible` i cap control amagat**: la llista no té fills, sinó una taula de dades. Una llista feta de
  controls `ui.row` és possible, però llavors les files que no toquen es dibuixarien igualment i el que s'hauria de
  fer és una altra cosa (docs/90).

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
ui.present('gc16')
```

### `ui.textbox` i el teclat del sistema (0.72.0)

**El cursor és la meitat important d'una caixa de text**, i aquí és un **bloc tan ample com la lletra que té a
sota**, pintat amb la tinta i amb aquella lletra en paper —un bloc ple taparia la lletra que es va a escriure a
sobre, i un contorn seria una caixa dibuixada sobre una altra caixa—.

- **El text és una finestra**: si la línia no hi cap, cauen les lletres del començament fins que el cursor queda
  al vidre. Sense això, qui escriu al final d'una paraula llarga escriu en un lloc que no es veu (docs/90).
- **El parpelleig és de l'app**: el control no té rellotge, i un control amb rellotge és un control que continua
  pintant quan ningú no escriu. L'app gira `box.caret` en un torn lent i prou.
- **`mask`** dibuixa un puntet per lletra: és el mateix requadre amb els punts al lloc de les lletres, i per tant el
  cursor cau on cauria i l'amplada del que s'ha escrit no es filtra.
- **`cursor` és quants bytes hi ha abans del cursor**, no una lletra: una lletra pot fer dos bytes.

**El teclat no és un control, és del sistema.** És una **banda** que el nucli pinta a baix de la vista, sobre la
pàgina ja composta, i que **pren el dit i el dial abans que els controls de la pàgina**:

| Crida | Què fa |
|---|---|
| `kb.target = box` | La caixa on escriu. És l'únic camp que té |
| `kb:show('alpha')` · `kb:show('numeric')` | La banda surt, amb el joc de tecles que es demana |
| `kb:hide()` | La banda marxa (i marxa sola quan l'app s'acaba) |
| `kb:height()` | **La banda que pren** —176 al Paper—, que des de 0.84.0 ja no cal restar de res: és el que es pregunta per pintar una barra pròpia just a sobre |
| `kb:visible()` | Si és al vidre |

**La vista de l'app és el que queda sobre la banda** (0.84.0). `ui.caps().height` i `ui.height()` diuen **736 i no 912**
mentre el teclat és al vidre, i l'app rep **`on_resize(width, height)`** quan la banda surt i quan marxa —el mateix hook
d'una vista que canvia de mida o de gir—, o sigui que una pàgina que es fa a partir de `ui.caps()` ja queda bé i no ha de
restar res. El llenç de sota no es torna a demanar (no costa ni un byte: la banda es pinta a sobre dels seus últims 176
píxels), i `ui.canvas().height` continua dient l'alçada del llenç per a qui necessiti la mida física.

```lua
local kb = ui.keyboard()
kb.target = box
kb:show('alpha')            -- la banda primer
local view = ui.caps()      -- i la vista ja és el que queda: 736
local page = ui.panel(view.width, view.height, 'vertical')

function on_resize(width, height)   -- la banda ha vingut o se n'ha anat
  page:resize(width, height)
  ui.present('gc16')
end
```

- **L'app no mana cap tecla**: cada tecla arriba a la caixa que ha nomenat, la caixa escriu el camp i tot seguit
  avisa amb `on_change(self, text)`. El **13** no és cap lletra: és el `OK`, i la caixa diu `on_enter(self)` i no
  toca el text.
- **Les fletxes del dial caminen les tecles** i el centre prem la que hi ha: una placa de tres tecles també pot
  escriure. El recorregut va **per les tecles que es veuen**, en l'ordre en què es pinten, i no per una graella de
  caselles buides (0.83.0).
- **La tecla on ets s'inverteix** i la resta són lletres i prou: des de 0.83.0 el teclat no dibuixa cap botó, i el
  que diu «aquesta és la que prems» és el bloc de tinta.
- **Les lletres són una tipografia de debò** (0.85.0): `DejaVu Sans Bold` de `/fonts`, dibuixada amb
  `font::load_ttf`, i la cara 5 × 7 del sistema només quan el disc no porta `/fonts`. El joc alfa és un QWERTY
  esglaonat —la fila de la `a` va mitja tecla més a la dreta que la de la `q`— amb la **barra d'espai** al seu lloc,
  la tecla de **majúscules** (`MAY`) a l'esquerra de la `z` i la de **retrocés** (←) a la dreta de la `m`. Les tecles
  que no són lletres es dibuixen amb el signe i no amb la paraula: ← per esborrar, ↵ per l'enter, i una barra
  d'extrems rodons per l'espai.
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
  **parpellejava a cada sis-cents mil·lisegons**, que és el que l'app que hi escriu triga a fer parpellejar el cursor
  de la seva caixa (docs/50, docs/90).

- **Una pàgina que es tecleja ha de pintar el que ha teclejat, i amb una qualitat que neteja** (0.84.1). Un `du`
  deixa **cinc setzens** del que hi havia —és la gràcia del mode ràpid i el que fa que la pàgina es vegi com una
  pàgina de tinta— i en una caixa de text això són les lletres de la tecla anterior a sota de les noves, que és el que
  es va veure a `run typing` amb cinc tecles. El que fa una app que escriu és `ui.present('gl16')` al seu `on_change`
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
view = ui.caps()                  -- i el que la pàgina té és el que queda: 736 (0.84.0)
ui.present('gc16')
```

`run typing` és la pàgina sencera: una caixa, el teclat, el cursor que parpelleja i el que diu el `on_enter`.

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
ui.present('gc16')
```

`run dialog` és una pàgina amb una targeta i prou, per provar-la amb el dial i amb el dit.

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
ui.present('gc16')
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
ui.present('gc16')
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
- **Dos senyals, com al calendari**: l'opció de `primary` s'**inverteix** i el dial dibuixa **l'anella** dins de la
  caixa on és, en el to de les paraules, que és el que contrasta tant si la caixa és paper com si és tinta.
- **`tag`** és una paraula curta al davant de l'etiqueta («sys» per a una app del sistema, «lua» per a un guió) i va en
  el to `dim`, perquè el que una persona llegeix és l'etiqueta.
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
ui.present('gc16')
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
- **Un tick és una caixa amb el mig ple** (`checked`): dos tons i cap gris, o sigui que «està encesa» és la caixa que té
  alguna cosa a dins i «està apagada» la que està buida. **Qui sap l'estat és l'app**: ella escriu `checked` i torna a
  donar la taula d'`items`, que és sempre un repintat.
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
  { title = 'View', items = { {label = 'Wrap', checked = true} } },
}
bar.on_pick = function(self, title, entry)
  console.print('menu ' .. title .. ' entry ' .. entry .. ' (open ' .. self.open .. ')')
end
ui.present('gc16')
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
  a la mateixa aritmètica del dibuix.

```lua
local box = ui.textbox_multiline(500, 300)
box:place(24, 80)
box.scale = 2
box.wrap = true
box.text = 'Notes\n\nThe enter key starts a new line here.'
box.cursor = 0
console.print('rows ' .. box:rows())
local kb = ui.keyboard()
kb.target = box
kb:show('alpha')
box.on_change = function(self, text) saved = text end
ui.present('gc16')
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
    ui.present('gc16')
  end
  ui.present('gc16')
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
  ui.present('gc16')
end
ui.present('gc16')
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

```lua
local grid = ui.icon_grid(500, 400)
grid:place(20, 60)
grid.items = {
  { label = 'Notes', mark = 'N' }, { label = 'Clock', mark = 'C' },
  { label = 'Files', mark = 'F' }, { label = 'Paint', mark = 'P' },
}
grid.marked = 1
grid.on_pick = function(self, index) console.print('cell ' .. index) end
ui.present('gc16')
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
  **cinquanta mil·lisegons** que l'app va esperar. Mesurat al banc: `lua.call 2 51.1ms` i
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
- L'exemple és l'app `glass` (`run glass`), que llegeix el panell en bucle i escriu els dits i els
  gestos.

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
- **Les posicions són les de la placa i no les de la pantalla**: la Paper no té pantalla encara, i el
  dia que en tingui la rotació no canviarà on és la tecla (docs/50).
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
| `power.battery()` | `{mv, pct, charging}` o `nil` | `nil` és **"aquesta placa no ho sap"**, i no és el mateix que zero volts. `mv` és una mesura i `pct` una **corba** (una cel·la de liti no és una línia recta, `hal/battery.h`); `charging` només és cert si la placa ho pot preguntar, i al Paper i al banc és sempre fals. **De mà**: `run battery` (`apps/battery/`) ensenya els dos camins |
| `power.sleep(opts)` | **promesa** | `{mode="light" o "deep", in_ms=<ms> o until=<època en segons>}`; es resol `true` quan la placa torna |
| `power.keep_awake(b)` | booleà | L'app diu que està fent una cosa que no s'ha d'interrompre |
| `power.cpu()` | número | La velocitat del processador, en MHz |
| `power.cpu(mhz)` | número o `nil, error` | La canvia. **Els passos són 80, 160 i 240**: un altre número es refusa amb `E_INVAL` i **no s'arrodoneix** |
| `power.off()` ⏳ | booleà | Apaga la placa de debò: talla el corrent, no arma res i només el botó la torna a encendre. **Al prompt ja hi és** (`poweroff`, que diu si la placa no en té el control, docs/50); des de Lua encara no, com `sys.reboot()` |

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

L'exemple de mà és `run sleepy` (`apps/sleepy/`), que dorm cinc minuts, desa quantes vegades ho ha
fet i ho torna a llegir quan la placa arrenca. I `run rtccheck` (`apps/rtccheck/`) és el que
mesura: diu d'on surt l'hora, què pot despertar la placa, on és la bateria, quant ha durat de debò
una espera d'un segon i, després d'un son de vint segons, **quants mil·lisegons s'ha desviat** el
despert de l'hora demanada.

## `log` ⏳

**Avui no hi és.** El contracte: un anell de línies que una app pot escriure i tornar a llegir, sense
arribar mai a la flash.

| Crida | Notes |
|---|---|
| `log.debug(fmt, ...)` / `log.info` / `log.warn` / `log.error` | Sintaxi de `string.format` |
| `log.dump([n])` | Torna les últimes `n` línies de l'anella com a cadena |

El log no arriba mai a la flash. Si l'app té el port sèrie, el log **no** surt per pantalla: queda a
l'anella i es pot llegir amb `log` des del prompt o amb `log.dump()`.
