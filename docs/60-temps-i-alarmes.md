# 60 · Temps i alarmes

## Dues menes de temps

| | Per a què | Pot saltar? |
|---|---|---|
| **Monòton** (`IClock::monotonic_ms`) | Durades, terminis, esperes | No, mai enrere |
| **De paret** (`IClock::epoch_ms`) | Dates, alarmes, «les 6:00» | **Sí**, quan arriba una sincronització |

Regla: per mesurar durades, sempre `sys.time_ms()`. `time.now()` és per dir-li a una persona quina
hora és.

## D'on surt l'hora de paret

Per ordre de preferència:

1. **Xip RTC amb pila** (M5Paper, DS3231 al Grove…): és la font de veritat quan hi és. **El Paper en té
   un**: la BM8563 de la I2C 0x51 amb la pila de botó, i per això `time.caps().source` hi diu `"rtc"`
   i l'hora sobreviu a treure el cable (docs/50). Als ATOM no n'hi ha, i allà la font és la 2.
2. **RTC del SoC**: compta mentre hi ha corrent, i també durant el son profund. Es perd si es treu
   l'alimentació.
3. **NTP**: quan hi ha xarxa. **No hi ha cap còpia de l'època a la NVS**: una placa que s'ha quedat
   sense corrent arrenca amb `source` = `"none"` i l'època a 0, que és la veritat i no una estimació
   que ningú ha mesurat. Corregeix el desfasament i, si el desfasament és gran, salta.
   **Des del 31/10 hi és de debò**: `net.ntp([servidor[, ms]])` des de Lua i `ntp [servidor]` des de la
   shell. Passa per la **cua de peticions** com la resta (una cosa alhora) i **no deixa la ràdio
   encesa** quan acaba. **Necessita xarxa**: si la placa no hi és, es refusa a l'acte
   (`ntp: the board is not on a network`) en comptes d'esperar un termini que ningú no pot complir. A la placa el client és el `esp_sntp` de lwip, que posa el rellotge del
   sistema ell mateix i avisa per una crida de notificació; el nucli només diu al rellotge quina hora
   és, perquè les dues bandes ho facin igual (l'emulador té un servidor de mentida que contesta al cap
   de `--wifi` ms). Prova: `test_ntp_sets_the_clock`, que mira que `time: 0` ja no surti.

`time.caps().source` diu quina s'ha fet servir: `"rtc"`, `"soc"` o `"none"`. `time.caps().synced` és
**fals sempre avui**: el codi el posa a `false` i el NTP, que ja hi és, no el mou (docs/90).

**Sense cap font ni sincronització**, l'època arrenca a `0` i el sistema ho diu: `time.caps().source`
és `"none"`. Una app que depèn de l'hora ha de mirar-ho abans de fer res estrany.

## La cua d'alarmes

Una sola llista a la RAM, **ordenada de la més propera a la més llunyana**. Hi conviuen:

- alarmes d'apps (`alarm.at`, `alarm.after`),
- temporitzadors del nucli (esperes internes, reintents),
- i el pròxim despertar del son.

De la llista, **només la primera** és la que es programa al maquinari:

```
  ara ──────────────────────────────────────────────────────────────▶
   [A: +2 s]  [B: +5 s]  [C: +1 min]  [D: +3 h]
       ▲
       └── això és l'únic que es demana a IClock::arm_wake()
```

Quan A passa, es programa B. Des del 31/10 el rellotge de les dues bandes **sap armar-se**:
`alarm.caps().wake` val `"yes"` o `"no"` (si aquesta placa pot armar un despert), l'ATOM programa el
temporitzador del RTC i l'emulador mou el seu rellotge. El que el sistema encara **no** fa és dormir-se
sol: el despert serveix quan algú ha demanat `power.sleep`, que és el que M6 hi ha posat (docs/90).

Aquesta ordenació és el que fa que el sistema només hagi de gestionar un despertador encara que hi
hagi desenes d'alarmes.

## Què és una alarma

```json
{ "id": 7, "app": "epub", "on": 1767225600, "repeat": "daily",
  "raise": false, "persist": true, "payload": {"page": 12}, "missed": 0 }
```

Es desa a NVS (espai `alarms`) quan `persist` és cert. Les alarmes **no porten codi**: ni funcions,
ni noms de funcions. Quan venç una alarma de l'app X, el sistema carrega X i crida
`on_alarm(id, payload, meta)`. Això és el que permet que una alarma sobrevisqui a un reinici i a la
mort de la VM.

Límit: `alarm.caps().max` alarmes vives alhora (valor a fixar quan es mesuri l'espai de NVS).

## Què passa quan venç

| Situació | Què fa el nucli |
|---|---|
| L'app propietària és a primer pla i té una tasca esperant | Li entrega un **esdeveniment**; la tasca el veu a `console`/`on_event` |
| L'app propietària és a primer pla però està calculant | Espera al punt de planificació següent (fi de torn) |
| L'app propietària no està carregada | **Finestra d'alarma** |
| L'app propietària no existeix (desinstal·lada) | Es descarta, amb una línia al log |

### La finestra d'alarma

1. Es comprova que el heap lliure arribi al sostre de l'app (`IPlatform::heap_limit_kb()`; si no, es
   descarta amb `E_NOMEM`).
2. Es crea la VM, es carrega l'entry, es crida `on_alarm(id, payload, meta)`.
3. Pressupost **més curt** que el d'una app de primer pla: és una finestra, no una sessió.
4. Es destrueix la VM. El primer pla continua exactament on era.

Dins de la finestra: `console.is_foreground()` és `false` i escriure a la consola no fa res (queda al
log). **Cap crida no dona `E_NOTFOREGROUND`**: el codi no el produeix enlloc i la taula d'errors de
`docs/30` el té marcat com a reservat.

Cost a mesurar: cada finestra paga l'arrencada de l'app (crear la VM i carregar l'entry). Per això
una app que vol ser barata en segon pla ha de fer que `on_alarm` no carregui mitja app i ha de tenir
el seu estat al disc (`fs.data`).

### Pujar al davant

Si l'alarma porta `raise = true`, llavors, en comptes de tancar-se:

1. L'app de primer pla es tanca net: `on_pause()`, `on_save()`, `on_close()`.
2. L'app de l'alarma **es queda** com la de primer pla, amb la VM que s'estava fent servir a la
   finestra.

## Alarmes perdudes

Si el sistema estava apagat quan tocava:

- En arrencar, totes les alarmes vençudes es reprodueixen **en ordre**, amb `meta.late_ms` dient quant
  s'han endarrerit i `meta.missed` quantes repeticions s'han perdut.
- Una repetició `"daily"` que s'ha perdut tres dies arriba **una vegada**, no tres.
- Les alarmes que es reprodueixen es tracten com una finestra d'alarma normal, una darrere l'altra.

## Repetició

- `repeat = <segons>` o `"daily"` (cada dia a la mateixa hora local).
- Després de disparar-se, es reprograma a la següent ocurrència **encara que l'app hagi fallat**.
- Si `app.raise()` puja l'app, la repetició continua programada.

## Son

| Mode | Qui ho decideix | Què es desa |
|---|---|---|
| `light` | L'app (`power.sleep`) | Res: la RAM es manté |
| `deep` | L'app (`power.sleep`) o **el nucli** (la política d'oci, §«Qui arma el despert») | La nota de sessió (quina app hi ha al davant, amb quin fitxer i a quina hora s'ha de despertar) a la NVS, i les alarmes persistents |

Abans de dormir, el nucli passa per `on_pause()` i `on_save()` de l'app oberta, i després programa el
despertador amb la primera alarma de la cua. Quan desperta, mira el motiu (`last_wake`) i segueix des
de l'arrencada normal: **la VM no sobreviu a un son profund**, es reconstrueix.

**Un son lleuger el pot tallar un botó o la propera alarma.** El botó ja hi era; l'alarma és nova:
com que l'alarma és del sistema i no ha de fer cua darrere d'una app que dorm, `kernel::power` baixa
el termini del son fins a l'alarma més propera. La promesa es tanca igual (`true`), i l'app que
necessiti el descans sencer l'ha de tornar a demanar.

**Un son profund no el talla res.** Si una app en demana un de més llarg que la propera alarma, la
placa dorm fins al seu termini i l'alarma s'entrega tard (`late_ms` ho diu). Fer-ho bé vol decidir
qui mana —l'app que dorm o l'alarma del sistema— i no s'ha decidit encara (docs/90).

**Les dues fondàries hi són a les dues bandes**, i la placa ho diu: `power.caps().sleep_light` i
`power.caps().sleep_deep` són certs, i `power.caps().wake` diu de què es pot despertar (`"timer,
button"` a l'ATOM, `"timer, clock"` al Paper). I el sistema **sí que es dorm sol** al Paper, quan hi
ha una alarma pendent i ningú no fa servir la placa (§«Qui arma el despert»); a les plaques sense xip
de rellotge no, perquè un caràcter que arribi mentre dorm es perd i no hi ha pantalla per dir-ho.

## Precisió

| Què | Valor |
|---|---|
| Granularitat de la cua | 1 ms a la RAM |
| Precisió de l'entrega | **Mesurat a la placa el 31/10**: una alarma a 2 s rebuda amb el sistema despert surt **`late 0 ms`** al Paper. Al son és **pendent**: amb el cable posat el son és el del xip i el número encara porta el cost d'arrencar. A l'emulador, `late 3 ms` (30/10) |
| Alarmes amb el sistema despert però ocupat | Com a màxim, un torn del nucli |

No es promet cap número de precisió fins que no es mesuri a la placa, i quan es mesuri s'ha de dir
amb què s'ha mesurat.

## Qui arma el despert: el rellotge més exacte que hi hagi

**Dormir és l'única manera d'estalviar bateria**, i tot el que allarga el temps despert és una
despesa. Per això hi ha una regla, i no és de gust:

> **El despert l'arma el rellotge més exacte que tingui la placa.** Si hi ha un rellotge de debò amb
> la seva alarma (la BM8563 del Paper), l'ha d'armar ell; el temporitzador del RTC del xip és el que
> hi ha quan no n'hi ha cap, no el que es prefereix.

Per què, amb els números a la mà i els que falten:

- El temporitzador de son de l'ESP32 compta amb l'**oscil·lador RC intern** d'aquest build
  (`CONFIG_ESP32_RTC_CLK_SRC_INT_RC=y`, i cap placa del projecte no hi té els 32 kHz), o sigui que
  **deriva**. El rellotge mentre el sistema és despert no deriva (va amb el cristall de l'APB): el que
  deriva és **quant ha durat el son**.
- Un despert que deriva obliga a **despertar-se abans** per assegurar-se d'arribar-hi a temps, i cada
  minut despert de més és bateria que no s'ha estalviat. La BM8563 va amb **quars**, i la seva alarma
  **sí que es pot fer servir**, però no per despertar el xip: el seu `INT` no arriba a cap GPIO, sinó
  al circuit d'alimentació, o sigui que el que fa és **apagar la placa del tot i tornar-la a
  encendre** a l'hora demanada (docs/50, «Apagar-se de debò»). Amb el rellotge del xip armant el
  despert cal marge; amb la BM8563, el marge es pot treure —quan es mesuri.
- **Quant deriva no està mesurat**, i per això és una feina pendent amb criteri propi (docs/90): un
  son profund de deu minuts i comparar l'hora demanada amb la de tornada, que la diu el rellotge que
  no deriva. **El número ja es mesura sol i es desa**: el nucli compara l'hora a què el son havia de
  tornar amb la que diu el rellotge en despertar, ho diu per la consola i ho deixa a la NVS, i
  `power last` el torna a ensenyar quan la línia ja ha passat. El que falta és la dormida de deu
  minuts en aquesta placa, no el mecanisme.
- **`power last` diu l'última mesura i no la millor**: `+1234 ms (timer)` vol dir que el son va
  acabar un segon i quart després de l'hora demanada, i el parèntesi diu què el va despertar. `(clock)`
  és el millor dels casos: la placa estava **apagada** i la va tornar a encendre l'alarma del
  rellotge de quars (docs/50).
- **Un son que no arriba a la seva hora no és cap mesura, i es diu així** (`(timer, cut short)`,
  31/10). Si el que torna la placa no és ni el temporitzador ni l'alarma del xip —un dit al botó, un
  cable que s'ha posat, una alimentació que ha parpellejat—, el son no ha arribat enlloc i el número
  només és **quant abans** es va tallar: la consola diu `the sleep was cut short by …` i el parèntesi
  porta `cut short`, perquè una precisió mesurada en un son que mai no s'ha fet seria una mentida
  sobre el rellotge. Va sortir al vidre el 31/10: un son de 300 s va tornar als 56 s amb la causa
  sense amo i el nucli ho va presentar com una deriva de `−243 530 ms`, que és el que sembla una placa
  trencada i era una placa que no havia arribat a provar res.
- **L'arrencada no és part de l'error del rellotge** (`hal::wake_error_ms`, 31/10). Una placa que es
  reinicia no informa en el moment de despertar-se: abans hi ha el carregador d'arrencada, el
  banner, la flash, la targeta i els ajustos, i això és **al voltant d'un segon de firmware**. Al
  Paper, un son de 20 s va sortir `+1 191 ms` i `+1 236 ms` en dues tandes, que és un **6 %** i no ho
  és: és el firmware comptant-se a si mateix. El nucli ara **resta el temps des de l'arrencada**
  (`monotonic_ms`) quan el despert ve d'un son de debò, i deixa el número net. El que queda dins és
  el carregador d'arrencada, que es mesura a part (`docs/95`, E-3).

El que **no** canvia amb això: la cua continua essent de software (`lib/alarms`), perquè una alarma
aquí és un objecte —amo, payload, repetició, `late_ms`, persistència— i un registre dins d'un xip de
rellotge no en sap res. El maquinari té una sola feina: **despertar el xip al moment exacte**. I la
cua ja arma **un sol** despertador, que és exactament el que el xip del rellotge pot fer.

Com està avui:

1. **L'`INT` no és un GPIO, però sí que està cablejat**: va al circuit d'alimentació, així que no
   serveix per despertar el xip sinó per **tornar a encendre la placa**. Quan el Paper ha de dormir,
   arma l'alarma (`Bm8563::arm_at`) i baixa GPIO2: la placa s'apaga de debò (`deep_powers_off()` és
   cert en aquesta placa, docs/50).
2. **És el sistema qui ho decideix, no una app.** El nucli, a cada torn, mira si hi ha una alarma
   pendent i, si ningú no l'està fent servir, **s'apaga fins a aquella alarma** (`kernel::power`).
   O sigui que una alarma d'app en una placa amb rellotge ja no la despert cap temporitzador de
   software: la compta el quars. El chip té dues maneres d'avisar i s'usa la més exacta que arribi
   (docs/50): el compte enrere del xip va **al segon** fins a 4 h 15 min, l'alarma de rellotge va al
   minut per a més enllà, i una espera de més d'un dia s'arma **en tandes** —la placa es desperta
   abans d'hora, no hi troba res i torna a armar—, o sigui que el quars compta **totes** les esperes.
   Els números de la política, que són decisions i no mesures: deu segons de consola en silenci, una
   espera mínima de deu segons i cap app ni `keep_awake` enmig. `power idle off` la desactiva mentre
   una persona treballa al prompt.
3. **Rearmar-lo a cada despert**: el xip dispara una vegada. A l'arrencada es llegeix el bit `AF` —per
   saber si va ser l'alarma o el botó `PWR`— i es desarma (`RtcClock::adopt`).
4. **Un sol despertador, i el té el xip.** La cua està ordenada i només s'arma la seva entrada més
   propera, com es feia amb el temporitzador del SoC: la diferència és que ara qui la té armada és
   el xip. Quan en canvia la propera, `RtcClock::arm_wake` escriu l'hora al xip, l'arma i **baixa el
   temporitzador del SoC**, perquè dos armats per a la mateixa data són dues veritats i una seria
   falsa. El del SoC només s'arma quan el xip no pot dir res (no contesta, no té hora, o la data
   se li escapa), i llavors el xip es desarma.
5. **Quan sona, se n'arma la següent.** El xip dispara una vegada: a l'arrencada es desarma, la cua
   es torna a carregar de la NVS, l'alarma que tocava s'entrega i `arm_nearest_alarm()` hi posa la
   següent. És el mateix ball que feia el temporitzador simulat, amb el quars al mig.
6. **Una tanda no és el despert.** Si el rellotge ha despertat la placa abans de l'hora demanada (una
   espera de més d'un dia, armada en tandes), el nucli **no dona l'app per desperta ni la torna a
   obrir**: desa l'hora que falta, diu quant queda i s'apaga un altre cop, sense esperar els deu
   segons de consola en silenci. Quan la data arriba de debò, llavors sí que es mesura i l'app torna
   amb `restore` (docs/30).
7. **L'hora torna del xip.** Després de cada son que torna, el nucli demana `IClock::resync()`: al
   Paper torna a llegir el BM8563 i corregeix el comptador del SoC, perquè el son és exactament
   quan el comptador deriva i el quars no.

## Què hi ha avui

| Peça | Estat |
|---|---|
| Rellotge amb època | El que compta és sempre el **del xip**: `gettimeofday` per a l'època i `esp_timer_get_time` per al monòton, amb la marca a la memòria del RTC perquè un son profund no la perdi. Mentre ningú no l'ha posada, `source` és `"none"` i l'època val 0 (el `gettimeofday` de l'ESP32 compta des de l'arrencada i fer-lo passar per època seria mentir). **Al Paper hi ha a sobre la BM8563 amb pila de botó** (`RtcClock`): es llegeix a l'arrencada i **després de cada son que torna**, s'hi escriu a cada `set_epoch` i a cada armat del despert, i mentre ella és la que recorda `source` diu `"rtc"` i l'hora sobreviu a treure el cable. L'emulador: `--now` posa l'època, i des de la 0.59.0 l'hora que el xip recorda **sobreviu entre execucions** —`<state>/clock.bin`, amb el temps que la placa ha estat desendollada comptat— al Paper (`"rtc"`) i als ATOM només amb `--persist` (`"soc"`, que és un reinici i no una placa desendollada); sense cap de les dues, val 0 i diu `"none"` |
| `time.*` | `now`, `now_ms`, `utc`, `date`, `tz` i `caps`. Formats, noms de zona i `set` no hi són (docs/30) |
| Cua única ordenada | `lib/alarms`, 8 entrades de 112 B (896 B). Només la primera s'arma, i `arm_wake` es crida quan canvia la més propera |
| `alarm.at`/`after`/`cancel`/`list`/`caps` | Hi són. Una app només cancel·la les seves, i el payload és cadena o número fins que hi hagi un codificador JSON |
| Entrega | `on_alarm(id, payload, {late_ms, missed})` entre dos torns, amb la coroutine suspesa |
| Finestra | VM pròpia, un torn, la crida i la VM fora. `console.is_foreground()` hi és `false` i **l'entry s'hi executa igualment**: una app que arma alarmes al seu chunk ha de mirar el flag, o el sistema es crida a si mateix per sempre |
| `raise` | L'app del davant es tanca amb els seus comiats (`on_pause`, `on_save`, `on_close`) i la de l'alarma queda al davant |
| Repeticions | Una diària perduda tres dies arriba **una vegada**, amb `missed = 2` |
| `persist` | La cua es desa a la NVS amb una **època per línia**, i les vençudes es disparen en arrencar amb el seu `late_ms`. Sense època no s'escriu res |
| Despert | `can_wake()` és cert a les dues bandes, i a l'ATOM el despert és el temporitzador del RTC i el botó. Una alarma no es pot impedir que es perdi: si no hi ha corrent, es reprodueix en arrencar |

## L'hora de la persona: un desfasament i no una zona (0.125.0)

El xip compta **UTC** —és el que guarden la BM8563 i el RTC del SoC, i el que arriba del NTP— i una persona llegeix
la seva hora. L'únic número que les uneix és **quants minuts la local va per davant d'UTC**, i viu a `kernel::cfg` com
qualsevol altre ajust: `cfg tz +01:00` des de la consola, `time.tz('+01:00')` des de Lua, la mateixa clau de la NVS
(`tz`, escrita com el text que es va teclejar perquè una persona la pugui llegir amb `nvs`) i el mateix parser.

- **`time.utc([t])` és el que compta el xip; `time.date([t])` és el que llegeix la persona**: la segona és la primera
  amb el desfasament sumat, i amb zero (el valor per defecte) les dues diuen el mateix.
- **El desfasament va de -14:00 a +14:00** i res més enllà: una hora setze hores per davant és una errata i no un lloc.
  Un valor desat que no es pot llegir —una build anterior, un text trencat— torna a UTC i ho diu al registre, perquè un
  rellotge dotze hores desviat en silenci és pitjor que un que no s'ha mogut mai.
- **No hi ha taula de zones i no se'n inventa cap**: el mapa del món és un megabyte i això dos bytes. Una placa on
  l'hora canvia dues vegades l'any s'ha de dir el número nou dues vegades l'any, i `cfg tz` ho fa en un segon.
- **`wday` continua sent 1 per a dilluns a 7 per a diumenge** i la regla dels traspàs continua vivint una sola vegada:
  `time.utc`/`time.date` demanen el dia de la setmana a `kernel::calendar` i el dia de l'any a `hal::days_from_civil`
  (docs/30).

Les proves són `test_the_timeclock_of_the_paper_project_keeps_the_day` (l'app que llegeix l'hora, a `test_apps`) i el
cas del rellotge de `test_calendar`, que ja dona la volta a l'època.

## El calendari: la regla dels anys de traspàs, una sola vegada (0.75.0)

L'època és un número i un calendari necessita dos més: **quants dies té un mes** i **quin dia de la setmana cau una
data**. Els dos viuen a `lib/kernel/src/calendar.cpp` (`kernel/calendar.h`) i enlloc més:

- `days_in_month(year, month)` — la regla sencera és quatre línies: **cada quatre anys, excepte els segles, excepte
  cada quatre-cents**. El 2000 va ser de traspàs i el 1900 no; el 2100 no ho serà.
- `weekday_of(year, month, day)` — el mètode de Sakamoto, aritmètica entera i dotze bytes de taula. **1 és dilluns i
  7 és diumenge**, perquè la setmana d'aquest sistema comença en dilluns.

Per què és al nucli i no a cada app: el vell tenia la mateixa regla a `system/wall_clock` precisament perquè **dues
còpies de la regla dels traspasos es donen respostes diferents** i un calendari que discrepa amb si mateix ho fa dues
vegades l'any. `ui.month` i el dial del calendari pregunten aquestes mateixes funcions (docs/30, docs/90). Les proves
són a `test/test_calendar`: el febrer de 2024, 1900, 2000 i 2100, i sis dates que un calendari ja ha imprès.

**I les dues coses que sí que són una opció** (0.170.0): el primer dia de la setmana i el dia de traspàs són
**ajustos del sistema**, i no una taula de cada app, perquè `ui.month`, `time.utc`/`date` i qualsevol pàgina que compti
dies d'un mes han de dir el mateix (l'app de la zona horària les mou amb `time.leap` i `time.week`, docs/30):

- **`cfg leap on|off`** (o `time.leap`): amb el traspàs apagat, `days_in_month` diu que el febrer té vint-i-vuit dies
  **cada any**, i la pàgina d'un calendari ensenya vint-i-vuit. **El dia de la setmana d'una data no es mou**: el 29 de
  febrer segueix caient on cau, perquè això és el cel i no una preferència.
- **`cfg week monday|sunday`** (o `time.week`): `weekday_of` continua tornant 1 = dilluns i 7 = diumenge —el compte no
  canvia— i el que gira és **la columna**: `ui.month` posa el primer dia del mes on li toca segons aquest ajust
  (`cells_of`, a `lib/kernel/src/ui.cpp`) i gira els set noms de la setmana amb ell. Dilluns és el valor amb què aquest
  sistema va néixer i diumenge és l'altre; una setmana que comenci en dimecres no s'accepta.
- **Les dues es desen a l'acte i sobreviuen a un reinici**, com el desfasament de l'hora: són claus de l'cfg (`leap`,
  `week`) escrites amb les paraules que una persona llegeix amb `nvs` (`on`/`off`, `monday`/`sunday`).
