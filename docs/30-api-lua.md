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

Als ATOM d'avui `caps` respon **13 de 18**, el Matrix **14 de 18** perquè hi afegeix `imu`, el Paper
**13 de 18** amb el SHT30 contestant i sense bateria ni targeta —**14** amb la bateria llegida **o** la
targeta, **15** amb totes dues, que és el sostre d'aquesta placa: les deu del xip, `littlefs`, `sd`,
`input`, `battery` i `env`— i l'emulador **el que diu el model que imita**: 13 al model de l'ATOM (12
si el terminal no és ANSI), 14 al del Matrix i 14 al del Paper (13 sense ANSI), que ja porta la
targeta `emulator/sd` i puja a 15 amb `--battery-mv` (docs/50, docs/85). L'única que no té ningú és
`display`, que encara no implementa cap placa. Els números del Paper **no són una previsió**: l'1/11
la placa va dir **15 de 18** amb la 0.10.1 (bateria i targeta dins) i `run air` va donar
`air 21.1 C 63.5 %` (docs/95, E-5).

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
| `app.args` | taula o `nil` | `{path, ext, source}` quan l'app s'obre per obrir un fitxer, o quan el prompt la hi apunta: `run hexdump /fram` i `open hexdump /fram` són el mateix per a l'app |
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
| `console.read_byte()` ⏳ | enter 0–255 o `nil` | |
| `console.on_data(fn)` ⏳ | — | Mode push: `fn(tros)` per cada tros que arribi |
| `console.is_foreground()` | booleà | `false` dins d'una finestra d'alarma |

**Avui hi ha** `console.print`, `console.write`, `console.read_line` i `console.is_foreground`.

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
  `data/apps/alarmqueue/`.
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

Tres exemples més, i cada un ensenya una cosa diferent: `run alarmqueue` (`data/apps/alarmqueue/`)
obre la **cua del sistema** —quantes n'hi caben, quines hi ha i en quin ordre, cancel·lar-ne una del
mig—; `run timers` (`data/apps/timers/`) arma tres temporitzadors i demostra que l'app suspesa no
atura el nucli; i `run alarmset` (`data/apps/alarmset/`) demana per consola quants segons falten i
arma una alarma **persistent i amb `raise`**, que és la que fa que el sistema apagui la placa i la
torni a encendre amb el rellotge (docs/60). I `run later` (`data/apps/later/`) és el cas complet:
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
- **El TLS no es pot desactivar i cap certificat viu al repositori**: la placa fa servir el paquet
  d'autoritats que porta el build d'Arduino, i un servidor que no hi és sota rebota amb `E_TLS`
  (docs/50).
- **Codis**: `E_NONET` (la placa no té cap xarxa desada on connectar-s'hi), `E_TIMEOUT`, `E_TOOBIG`,
  `E_TLS`, `E_HTTP`, `E_NOSPC` i `E_BUSY`.
- **La ràdio no queda encesa**, i la pàgina no es queda enlloc: el cos viu dins la cadena de Lua i,
  quan l'app el deixa anar, el recull el recolector.

```lua
local ok, page = pcall(function() return http.get('https://example.com/'):await() end)
if not ok then
  console.print('no page: ' .. tostring(page and page.code))
  return
end
console.print(page.status .. ' (' .. page.bytes .. ' bytes)')
console.print(page.body)
```

L'exemple de mà és `run page` (`data/apps/page/`), que baixa una pàgina i n'ensenya l'estat i el cos.

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
  'w')` i `file:write(…)`— i no amb `fs.write`, que és atòmic i necessita un reanomenament que una
  memòria no té. `fs.space` diu la mida de cadascuna i tota lliure, perquè no s'hi assigna res
  (docs/50, docs/70).
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
  del mig del dial. Les altres són de l'app i el sistema no hi té cap gest.

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
  lateral no tanquen res**: són d'un menú, no d'una sortida.
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
| `power.battery()` | `{mv, pct, charging}` o `nil` | `nil` és **"aquesta placa no ho sap"**, i no és el mateix que zero volts. `mv` és una mesura i `pct` una **corba** (una cel·la de liti no és una línia recta, `hal/battery.h`); `charging` només és cert si la placa ho pot preguntar, i al Paper i al banc és sempre fals. **De mà**: `run battery` (`data/apps/battery/`) ensenya els dos camins |
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

L'exemple de mà és `run sleepy` (`data/apps/sleepy/`), que dorm cinc minuts, desa quantes vegades ho ha
fet i ho torna a llegir quan la placa arrenca. I `run rtccheck` (`data/apps/rtccheck/`) és el que
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
