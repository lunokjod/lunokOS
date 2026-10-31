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

Encara no hi és, i per tant una app que ho cridi peta amb «attempt to call a nil value»:
`sys.reboot()`, `console.flush()`, `console.read()`,
`console.read_byte()`, `console.on_data()`, `console.is_foreground()`, i el
mòdul `app`. La llista és el que queda de M1 (`docs/90`).

`term.width()` i `term.height()` tornen **80 i 24**, que és el valor per defecte del contracte: el
fitxer `cfg/console.json` encara no es llegeix.

## Errors

| Codi | Quan |
|---|---|
| `E_UNSUPPORTED` | La placa no té la capacitat que demana la crida |
| `E_NOENT` | El camí o la clau no existeix |
| `E_EXIST` | Ja existeix i no es pot sobreescriure |
| `E_NO_HANDLES` | El pool de fitxers oberts és ple |
| `E_INVAL` | Paràmetre dolent |
| `E_TIMEOUT` | Ha trigat més del que s'havia dit |
| `E_IO` | Error del disc o del port |
| `E_NOMEM` | S'ha exhaurit el pressupost de memòria de l'app |
| `E_PERM` | El manifest no declara el permís |
| `E_TOOBIG` | El resultat no cap al sostre permes |
| `E_NOSPC` | No queda lloc: el disc, la quota de l'app o la memòria per a la resposta |
| `E_TLS` | El certificat del servidor no és acceptat, o la connexió xifrada no s'ha pogut fer |
| `E_HTTP` | El servidor no hi era, o ha plegat abans d'acabar de respondre |
| `E_BUSY` | Ja hi ha una operació en curs en aquest recurs |
| `E_NOTASK` | S'ha cridat `await` fora d'una tasca |
| `E_NOTFOREGROUND` | L'app és a la finestra d'alarma i ha demanat una cosa de primer pla |
| `E_BUDGET` | S'ha exhaurit el pressupost d'instruccions del torn |

## Capacitats

`sys.caps()` torna una taula plana. Cada clau és `true` o `false`:

`console`, `ansi`, `display`, `input`, `led`, `imu`, `littlefs`, `sd`, `nvs`, `rtc`, `rtc_wake`,
`net_wifi`, `net_http`, `net_prov`, `battery`, `sleep_light`, `sleep_deep`.

Als ATOM d'avui `caps` respon **13 de 17**, i el Matrix **14 de 17** perquè hi afegeix `imu`
(docs/50).

---

## `sys`

`sys` és una taula plana: el que és un valor es llegeix sense cridar res, i el que fa feina és una
funció.

| Nom | Torna | Notes |
|---|---|---|
| `sys.name` | cadena | `"lunokOS"` |
| `sys.version` | cadena | La versió del sistema, la mateixa que respon `version` |
| `sys.board` | cadena | El nom de la placa **tal com surt a `board =` de `platformio.ini`** (avui `m5stack-atom`); al PC, `emulator` |
| `sys.emulator` | booleà | Cert només al PC |
| `sys.lua` | cadena | La versió de Lua que porta el sistema |
| `sys.caps()` | taula de capacitats | Vegeu a dalt |
| `sys.heap()` | `{free, used, limit, peak}` | Bytes del pressupost de l'app |
| `sys.gc("step")` | — | Fa un pas de col·lector |
| `sys.gc("full")` | — | Col·lecció completa (cara; no es fa servir enmig d'un dibuix) |
| `sys.gc("count")` | enter | KB en ús |
| `sys.delay(ms)` ⏳ | promesa | Espera sense tocar la cua d'alarmes ni el port sèrie |
| `sys.time_ms()` | enter | Rellotge monòton en mil·lisegons. **Mai salta enrere** |
| `sys.reboot(reason)` | no torna | Permís: cap |

## Saber on s'executa

Una app pot acabar corrent en una placa que no ha vist mai. `sys.board` és l'única cosa que li diu
on és, i és **el nom que la placa té a `platformio.ini`** (`board = m5stack-atom`), no cap àlies
inventat pel sistema. Al PC no hi ha cap placa i no hi ha cap línia `board =`, i per això l'emulador
respon `emulator`.

| `sys.board` | Què hi ha | Què no hi ha |
|---|---|---|
| `m5stack-atom` | consola UART, LittleFS, NVS, un port Grove I2C | pantalla, bateria, teclat, targeta SD, xarxa |
| `emulator` | consola, LittleFS en un directori del PC | res de maquinari de debò: ni rellotge, ni flash, ni port sèrie |

`sys.emulator` és el mateix que preguntar `sys.board == "emulator"`, i és cert només al PC.

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

- **La comprovació grossa és el nom de la placa**; quan `sys.caps()` hi sigui, la comprovació fina serà
  per capacitat. El nom serveix per als casos en què canvia **la manera** de fer servir el maquinari,
  no només si hi és o no.
- **Un nom desconegut no es pot ignorar.** Si `sys.board` no és cap dels que l'app entén, ho diu i
  surt; no es queda provant maquinari a cegues.

## `app`

| Crida | Torna | Notes |
|---|---|---|
| `app.id` | cadena | El nom del directori de l'app |
| `app.args` | taula o `nil` | `{path, ext, source}` quan l'app s'obre per obrir un fitxer |
| `app.info()` | taula | El manifest de l'app mateixa |
| `app.spawn(fn)` ⏳ | promesa | Crea una tasca; la promesa es resol amb el que torni `fn` |
| `app.exit([code])` | no torna | Tanca l'app; `code` per defecte `0`, i la shell diu `app exit N` si no és zero |
| `app.raise()` | — | Demana passar a primer pla (només té sentit dins d'una finestra d'alarma) |
| `app.save(t)` | — | Desa una taula petita a `/data/<id>/state.json` |
| `app.restore()` | taula o `nil` | Recupera el que va desar `app.save` |

Avui hi ha `app.id`, `app.args`, `app.info()` i `app.exit`; la resta encara no, i cridar-les dona
«attempt to call a nil value». On van: `app.save`/`app.restore` a **M4** (necessiten escriure al disc
i un escriptor de JSON, que encara no hi ha), i `app.spawn`/`app.raise` a **M3** (tasques i finestra
d'alarma).

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
| `console.flush()` | — | Força la sortida |
| `console.read_line()` ⏳ | promesa amb la cadena sense `\n` | Una lectura alhora: la segona dona `E_BUSY` |
| `console.read(n)` ⏳ | cadena (fins a `n` bytes) o `nil` | |
| `console.read_byte()` ⏳ | enter 0–255 o `nil` | |
| `console.on_data(fn)` | — | Mode push: `fn(tros)` per cada tros que arribi |
| `console.is_foreground()` | booleà | `false` dins d'una finestra d'alarma |

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
| `time.sync()` ⏳ | promesa | NTP; requereix xarxa; permís `net` |
| `time.caps()` | `{source, synced, wake}` | `source` és `"soc"`, `"rtc"` o `"none"`; `wake` és `"none"`, `"light"` o `"deep"` |

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
| `alarm.at(spec)` | enter (id) | `spec` = `{on=època}` o `{in=ms}`, més `payload`, `repeat`, `raise`, `persist` |
| `alarm.after(ms[, payload])` | enter (id) | Drecera: alarma d'un sol cop |
| `alarm.cancel(id)` | booleà | |
| `alarm.list()` | taula | `{id, on, repeat, persist, raise}` de cada alarma |
| `alarm.caps()` | `{max, min_period_ms, wake}` | `max` alarmes simultànies |

Camps de `spec`:

- `payload`: qualsevol valor que es pugui desar com a JSON (petit).
- `repeat`: `nil` (un cop), un número (segons) o `"daily"`.
- `raise`: si és `true`, quan venç l'app passa a primer pla.
- `persist`: si és `true`, sobreviu a un reinici. Per defecte `true` si `repeat` hi és.

`meta` que rep `on_alarm` és `{late_ms, missed}`: quant tard s'ha entregat i quantes repeticions
s'han perdut mentre el sistema era apagat.

**Avui hi ha** `alarm.at`, `alarm.after`, `alarm.cancel`, `alarm.list`, `alarm.caps`, `on_alarm` i
`console.is_foreground()`. Tres límits que l'app ha de saber:

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

Encara no hi ha `persist`: una alarma viu a la RAM i es perd en reiniciar (docs/60).

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
| `fs.mounts()` | taula | `{path, type, rw}` de cada muntatge, sense l'espai (vegeu `fs.space`) |
| `fs.space([path])` | taula o `nil, error` | `{total, free}` del muntatge on viu el camí; `E_UNSUPPORTED` si el backend no ho sap |
| `fs.list(path)` | taula | `{name, type, size, mtime}` de cada entrada |
| `fs.stat(path)` | taula o `nil, error` | `{type, size, mtime}` |
| `fs.mkdir(path)` / `fs.remove(path)` / `fs.rename(a, b)` | — | Permís `sd_write` si és a `/sd` |
| `fs.open(path[, mode])` | objecte fitxer | `mode`: `"r"`, `"w"`, `"a"`, `"r+"` |
| `fs.read(path[, max])` | cadena o `nil, error` | Sostre dur del sistema (`E_TOOBIG`) |
| `fs.read_at(path, offset, len)` | cadena o `nil, error` | Accés aleatori sense obrir |
| `fs.write(path, data)` | enters | Escriu sencers (temporal + reanomenar) |
| `fs.data([sub])` | cadena | `/data/<id>/` o un camí a dins seu |

**Avui hi ha** `fs.open`, `fs.list`, `fs.stat`, `fs.read`, `fs.read_at`, `fs.write`, `fs.mkdir`,
`fs.remove`, `fs.rename`, `fs.data`, `fs.mounts` i `fs.space`.

De l'**objecte fitxer** (`f = fs.open(path[, "r"|"w"|"a"|"r+"])`):

- `f:read(n)`, `f:read("*l")`, `f:read("*a")`: "" i `nil` al final del fitxer, com a Lua.
- `f:write(s)` torna els bytes escrits, i **no és atòmica**: per deixar un fitxer sencer o res, `fs.write`.
- `f:seek(["set"|"cur"|"end"[, off]])` torna la posició nova; `f:size()`; `f:close()`.
- `f:flush()` no fa res: cada escriptura ja va al backend, i el fitxer és sencer quan es tanca.
- **Hi ha vuit fitxers oberts com a màxim**, i el novè dona `E_NO_HANDLES`. Les lectures i escriptures
  senceres (`fs.read`, `fs.write`) també obren i tanquen pel mateix camí, així que amb vuit fitxers
  oberts no hi cap res més fins que se'n tanqui un.

El que una app ha de saber:

- `fs.read` llegeix en **finestres de 512 B**: el fitxer mai no és sencer en un buffer de C, i el
  sostre per crida és de **4 096 B**; passar-se'n dona `E_TOOBIG` sense llegir res.
- `fs.write` és **atòmica**: escriu un temporal al costat i el reanomena. Si falla a mitges, el
  fitxer vell segueix sencer. Torna els bytes escrits.
- **Una app només escriu dins el seu terreny**, `/data/<id>/` (`fs.data([sub])` en dona el camí), i no
  pot llegir el d'una altra: això dona `E_INVAL`, no `E_NOENT`.
- **Quota: 64 KB per app**, comptant tots els seus fitxers. Passar-se'n dona `E_NOSPC` **abans** de
  tocar res. Com que el sostre de la VM també és de 64 KB, una sola cadena no hi arriba: la quota
  mossega acumulant fitxers.
- `fs.stat` torna `{type = "file"|"dir", size, mtime}`; `mtime` és 0 perquè cap backend el dona.
- `fs.list` torna una taula de `{name, type, size, mtime}` en l'ordre que dona el directori.
- **Per veure-ho de mà**: `run mtab` llista els muntatges i l'espai de cadascun.
- **`fs.mounts()` no pregunta l'espai**: la llista surt del que el nucli té muntat, i prou. Preguntar
  l'espai vol dir passejar pel sistema de fitxers, i una app que només vol la llista no ho ha de
  pagar.
- **`fs.space(path)` és la que ho pregunta**, i diu la veritat: `nil, {code = "E_UNSUPPORTED"}` quan
  el backend no ho sap. A l'emulador **sempre** diu això (el disc és el del PC i no és seu per
  informar-ne); a la placa, `/littlefs` i els que hi viuen damunt donen números i `/nvs` no.
- **Els camins amb espais van entre cometes** a la shell, no a Lua: `cat "res/el meu fitxer.txt"`.
  Des de Lua el camí és una cadena i els espais són espais.

Objecte fitxer: `f:read(n)`, `f:read("*l")`, `f:read("*a")`, `f:write(s)`,
`f:seek([whence[, off]])`, `f:flush()`, `f:size()`, `f:close()`. Si no es tanca, el recol·lector el
tanca, però el comptador de fitxers oberts és de 8: tancar és cosa de l'app.

## `store`

Clau-valor petit per app, sobre `/data/<id>/store.json`. Claus de 15 caràcters com a màxim.

| Crida | Torna |
|---|---|
| `store.get(k)` | valor o `nil` |
| `store.set(k, v)` | — |
| `store.remove(k)` | — |
| `store.all()` | taula |
| `store.clear()` | — |

## `net`

| Crida | Torna | Notes |
|---|---|---|
| `net.caps()` | `{wifi, http, tls, connected, ssid, ip, rssi}` | |
| `net.wifi.connect(ssid, pass[, timeout_s])` ⏳ | promesa | Permís `net` |
| `net.wifi.status()` | `{state, ssid, ip, rssi}` | `state`: `"off"`, `"connecting"`, `"up"`, `"fail"` |
| `net.wifi.disconnect()` | — | |
| `net.http.get(url[, opts])` ⏳ | `{status, headers, body}` | `opts`: `{headers, timeout_ms, max_bytes}`; el cos té sostre |

## `led`

El llum de la placa: **un** LED al ATOM Lite i **vint-i-cinc** al Matrix (5 per 5, un panell). El
contracte és el mateix als dos —`led.set` pinta **tot el llum** d'un color i `led.pixel` en pinta
**un**— i el que canvia és quants n'hi ha, que ho diu `led.caps().count` (docs/50).

| Crida | Torna | Notes |
|---|---|---|
| `led.caps()` | `{count, max, taken}` | Quants llums té la placa, **fins a quin valor** es pot encendre un canal, i si algú els ha pres |
| `led.set(r, g, b)` | `true` o `nil, error` | El color que vol l'app **mentre és al davant**, a tots els llums |
| `led.pixel(i, r, g, b)` | `true` o `nil, error` | **Un** llum: `i` compta des de zero, i un índex que la placa no té es refusa amb `E_INVAL` |
| `led.off()` | `true` o `nil, error` | El mateix que `led.set(0, 0, 0)`, i es llegeix millor |

- **El llum és del sistema i l'app el pren mentre viu.** El sistema hi diu **una sola cosa**: que la
  ràdio està treballant. Tota la resta és a la consola, i un llum encès costa mil·liampers que aquest
  sistema no gasta (docs/75).
- **Quan l'app se'n va, el llum s'apaga.** El color que havia pres **es deixa anar i no es torna al
  color d'abans**: un llum que sobreviu a la seva app és energia gastada per ningú. Passa igual si
  l'app acaba sola, si la tanquen amb tres tocs o si mor.
- **Al Matrix la imatge és de l'app i són vint-i-cinc llums.** Un `led.pixel` li dona el llum
  sencer, i és una imatge i no un color: vint-i-cinc crides fan un frame. `led.set` la torna a
  pintar tota d'un color si l'app canvia d'idea, i el que no passa mai és que la imatge quedi
  encesa quan l'app marxa — en marxar s'apaguen **tots** els llums (docs/50, docs/75).
- **En una placa sense llum aquest pas no costa res**: `led.set` respon `E_UNSUPPORTED` i deixar anar
  el color no fa res, perquè no hi ha res per apagar (docs/30).
- **El sostre d'un canal no és sempre 255.** `led.caps().max` diu quin és el de debò: **255** al
  Lite i **20** al Matrix, que és el número que dona el fabricant perquè vint-i-cinc LEDs de 2020
  en una caixa tancada s'escalfen i es poden fer malbé. El sostre el fa complir **el sistema**, no
  el driver: una app que demani 255 al panell en pinta 20, i la shell ho diu quan s'hi passa
  (`led: 00ff00 (this board stops at 20)`). Una app que vol una rampa que arribi dalt de tot l'ha
  de comptar amb `max`, com fan `light` i `matrix` (docs/50).
- **No cal fer res perquè es vegi**: el nucli el pinta a final de torn i només quan canvia. Un frame és
  una escriptura al fil i el llum també costa (docs/75).
- **Els colors són fluixos a posta** (`0,0,24` per a una app, `0,24,0` per a la ràdio, `24,0,0` per a un
  error): un llum que diu una cosa no ha de ser el més brillant de la taula.
- Amb `sys.caps().led` una app sap si tot això existeix abans de demanar-ho.

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
| `button.wait([ms])` | **promesa** | Es resol amb `{kind = "press"\|"long", held_ms}`; **20 000 ms** per defecte |

```lua
local press = button.wait(10000):await()
console.print('kind ' .. press.kind .. ', held ' .. press.held_ms .. ' ms')
```

- **Hi ha dues maneres de rebre una premuda i les tria l'app**:
  1. **Esperar-la** amb `button.wait(ms)`, que és una promesa: l'app no es congela, el nucli segueix
     girant i la resta del sistema funciona (docs/30).
  2. **Rebre-la** amb el hook **`on_button(kind, held_ms)`**, que es crida entre dos torns amb la
     coroutine suspesa —exactament com `on_alarm`— i per tant **no pot esperar**.
- **Si hi ha una espera oberta, guanya l'espera**: el hook només es crida quan ningú està esperant, i
  així una app no rep dues vegades la mateixa premuda.
- **`kind` és `"press"` o `"long"`**: `"long"` arriba **una vegada**, quan la premuda passa d'**1 s**, i
  no quan el dit s'aixeca.
- **El sistema es queda el gest de tancar**: **tres tocs dins d'1,5 s tanquen l'app del davant**, i es
  compten **abans** d'avisar l'app, o sigui que cap app no s'ho pot empassar. És el mateix gest que tres
  ESC a la consola, amb la mateixa finestra, perquè una persona no n'ha de recordar dos. El tercer toc
  **no arriba** a l'app: se la tanca.
- **Sense app al davant no passa res**: les premudes no es compten ni es guarden, i una app que arrenca
  no es troba un gest a mitges (docs/30).

## `power`

| Crida | Torna | Notes |
|---|---|---|
| `power.caps()` | `{sleep_light, sleep_deep, battery, charging, wake}` | Un bit només és cert si la placa ho té de debò; `wake` diu de **què** es pot despertar (`"timer, button"`, `"timer"` o `"none"`) |
| `power.battery()` | `{mv, pct, charging}` o `nil` | `nil` és **"aquesta placa no ho sap"**, i no és el mateix que zero volts |
| `power.sleep(opts)` | **promesa** | `{mode="light" o "deep", in_ms=<ms> o until=<època en segons>}`; es resol `true` quan la placa torna |
| `power.keep_awake(b)` | booleà | L'app diu que està fent una cosa que no s'ha d'interrompre |
| `power.cpu()` | número | La velocitat del processador, en MHz |
| `power.cpu(mhz)` | número o `nil, error` | La canvia. **Els passos són 80, 160 i 240**: un altre número es refusa amb `E_INVAL` i **no s'arrodoneix** |

- **Un son necessita una hora per tornar.** Sense `in_ms` ni `until` no hi ha res per amollar i es
  refusa amb `E_INVAL`: una placa que dorm sense hora espera un dit al botó (docs/60). També es
  refusa si la placa no sap quina hora és: primer `ntp` o algú que la posi.
- **`in_ms` i no `in`**: `in` és una paraula reservada de Lua i una taula amb `in=1000` ni tan sols
  s'analitza (`unexpected symbol near 'in'`, mesurat el 31/10).
- **El son lleuger torna i la promesa es tanca** amb `true`: la memòria, l'intèrpret i la consola hi
  són, i l'app continua exactament on era.
- **El son profund no torna.** Abans de parar el xip, el sistema **desa quina app hi ha al davant** i
  la tanca amb els seus comiats (`on_pause`, `on_save`, `on_close`); en arrencar, la torna a obrir
  amb **`app.args.restore = true`** perquè llegeixi el que va desar (docs/70, `fs.data`). Per això el
  que s'ha de recordar ha d'anar a **disc**: la RAM no sobreviu.
- **La promesa del son profund no es tanca mai**, i no és un defecte: l'app ja no hi és quan el xip
  torna. L'app que vol fer feina en despertar-se la fa a l'arrencada, mirant `app.args.restore`.
- **`power.keep_awake(true)`** queda apuntat i es pot consultar; el sistema **encara no es dorm sol**,
  o sigui que avui no canvia res, i es diu en comptes de fer creure que sí (docs/90).
- **On es desperta cada banda**: l'ATOM pel **temporitzador del RTC** i pel **botó** (`GPIO39`); el PC
  no dorm: mou el rellotge (docs/85). L'emulador **no es reinicia** després d'un son profund, i per
  això les proves criden `kernel::power::restore_session()` a mà.
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
fet i ho torna a llegir quan la placa arrenca.

## `log`

| Crida | Notes |
|---|---|
| `log.debug(fmt, ...)` / `log.info` / `log.warn` / `log.error` | Sintaxi de `string.format` |
| `log.dump([n])` | Torna les últimes `n` línies de l'anella com a cadena |

El log no arriba mai a la flash. Si l'app té el port sèrie, el log **no** surt per pantalla: queda a
l'anella i es pot llegir amb `log` des del prompt o amb `log.dump()`.
