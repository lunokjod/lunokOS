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

### Una promesa no s'hi pot posar dues vegades (0.189.172)

La cua és de vuit i una app que torna a armar el que ja tenia la gasta: dues alarmes de deu minuts amb el mateix payload
sonen dues vegades, i una que es repeteix i que el ganxo torna a armar **es dobla a cada volta** —una fa dues, dues en fan
quatre—. Per això `alarm.at` **pregunta abans d'afegir** (`alarms::find_same`) i contesta el que ja hi és:

- la configuració és el **payload, el període i els dos senyals** (`raise`, `persist`), i en una alarma d'un sol cop també
  **l'hora, comparada al segon**;
- una alarma que es repeteix **no té hora pròpia** —torna a la cua a la seva ocurrència següent— i per tant el període i el
  payload són tota la seva configuració;
- el que la crida torna és `id, true` i **no s'ha afegit res**; l'app decideix si això és el que volia o si esborra la vella
  (`alarm.cancel`) i en demana una de nova (docs/30).

I la lletra petita que aquesta porta va destapar: **`alarm.cancel` només esborra les alarmes de qui la crida**, o sigui que
una app que buida la cua ho ha de fer mirant el camp `app` de `alarm.list()`. La `battery` i el `clock` no ho miraven i
obrir-los s'emportava els avisos de l'`agenda` i del `meteo`, que els tornaven a armar a la càrrega següent: el que semblava
un avís que es multiplicava era mitja dotzena d'apps tornant-se a prometre el que una altra els havia esborrat.

### I tot el que li passa, al registre (0.189.149)

L'amo ho va demanar amb aquestes paraules: *«El log no informa sobre les alarmes … Quan es programa una alarma també ho
vull veure i a quin dispositiu s'envia o quin métode (si es un comptador intern o un RTC…)»*. Fins avui la cua només
escrivia quan una cosa anava malament, i el cas que funciona —l'app al davant amb el seu `on_alarm`— era mut. Ara hi ha
aquestes línies, totes amb l'etiqueta `ALARM`:

| Quan | Línia |
|---|---|
| Una app la posa (`alarm.at`, `alarm.after`) | `zzalarm adds #2; 2 of 8, the nearest is #2 (zzalarm) in 3 s`, i a sota **una línia per alarma** |
| Una app la treu (`alarm.cancel`) | `zzalarm cancels #3; 2 of 8, the nearest is ...` |
| Una baixa que no es pot fer | `zzalarm cannot cancel #3: it is not one of its alarms` |
| El nucli l'arma al maquinari | `#2 armed on the clock chip for 19:13:30 on 2026-11-16 (3 s from now)` |
| Una placa que no es pot despertar sola | `#2 is kept by the turn of the loop: this board cannot wake itself, so it fires only while it is on` |
| Sona | `#2 fired for zzalarm, 0 ms late, 1 left in the queue`, i després com s'ha lliurat |
| Arrenca amb alarmes desades | `1 restored from nvs`, i la cua sencera |

**`armed on` és la resposta al «quin métode»**: `the clock chip` és la BM8563 de la Paper —la seva alarma arriba al circuit
d'alimentació i el seu quars compta, o sigui que és l'únic que torna una placa amb el corrent tallat—, `the RTC timer of the
SoC` és el comptador del xip quan no hi ha rellotge amb pila, i **`the turn of the loop`** és el cas que no s'escrivia i el
que més importa: en una placa sense despert possible una alarma només sona amb la placa desperta.

El text de cada alarma el fa **la biblioteca** (`alarms::describe`) i no qui la crida: els camps són seus, i un segon lector
del mateix `struct` és el que discrepa el dia que hi arriba un camp. I la comanda **`alarm`** del prompt diu la mateixa cua
quan una persona la vol mirar, amb una línia que només el prompt pot donar perquè és del rellotge i no de la cua:
`wake: the clock chip`, `wake: the RTC timer of the SoC` o `wake: nothing armed` (docs/30, docs/50).

## Què és una alarma

```json
{ "id": 7, "app": "epub", "on": 1767225600, "repeat": "daily",
  "raise": false, "persist": true, "payload": "page 12", "missed": 0 }
```

Es desa a NVS (espai `alarms`) quan `persist` és cert. Les alarmes **no porten codi**: ni funcions,
ni noms de funcions. Quan venç una alarma de l'app X, el sistema carrega X i crida
`on_alarm(id, payload, meta)`. **El `payload` no és opcional**: és el que l'app rep del que ha sonat, i una alarma sense
cap és una que desvetlla la placa sense dir res (0.190.7, docs/30). Això és el que permet que una alarma sobrevisqui a un
reinici i a la mort de la VM.

Límit: `alarm.caps().max` alarmes vives alhora (valor a fixar quan es mesuri l'espai de NVS).

## Què passa quan venç

| Situació | Què fa el nucli |
|---|---|
| L'app propietària és a primer pla i té una tasca esperant | Li entrega un **esdeveniment**; la tasca el veu a `console`/`on_event` |
| L'app propietària és a primer pla però està calculant | Espera al punt de planificació següent (fi de torn) |
| L'app propietària no està carregada | **Finestra d'alarma** |
| L'app propietària no existeix (desinstal·lada) | Es descarta, amb una línia al log |

**I les alarmes d'una app desinstal·lada no haurien d'arribar aquí** (0.189.157): `apps remove <id>` i
`admin.uninstall(id)` esborren la carpeta **i les alarmes d'aquella app**, perquè una alarma que només pot servir per
despertar la placa per a una pàgina que no hi és és una pila gastada i un registre que diu `no window (app gone or no
on_alarm)` cada dia. La fila de dalt es queda com a xarxa: una targeta que va perdre la carpeta per fora —un `rm` des de
l'ordinador— no té cap camí on llançar l'esborrada, i el sistema l'ha de pair quan venç. **Les alarmes d'una app que
només ha canviat de versió no es toquen**: la cua és de qui la va omplir i no de qui la llegeix (docs/30, docs/40).

### La finestra d'alarma

1. Es comprova que el heap lliure arribi al sostre de l'app (`IPlatform::heap_limit_kb()`; si no, es
   descarta amb `E_NOMEM`).
2. Es crea la VM, es carrega l'entry, es crida `on_alarm(id, payload, meta)`.
3. Pressupost **més curt** que el d'una app de primer pla: és una finestra, no una sessió.
4. Es destrueix la VM. El primer pla continua exactament on era.

**I la pàgina del davant s'aparta per la finestra i torna quan ha acabat** (0.189.175, l'amo: *«la gràcia de les alarmes és
avisar a les apps quan toca»*). Amb un sol intèrpret una finestra no pot córrer a sobre d'una app viva, i la placa d'aquest
sistema en té una sempre que `cfg home` nomena una app —el llançador—; fins avui el sistema ho refusava i l'alarma d'una app de
segon pla es perdia sencera (`no window (app gone or no on_alarm)` a cada deu minuts, amb l'app ben present al disc: el registre
de l'amo del 18/11). El que fa ara:

- la pàgina es tanca **com la tanca un `raise`** —`on_pause`, `on_save`, `on_close` i la seva sessió al disc— i la línia del
  prompt no es diu, perquè no se n'ha anat;
- les alarmes que vencen al mateix torn comparteixen **una sola apartada**, i la finestra no toca el vidre: la vista és la de
  darrere d'una imatge (`ui.attach_under_a_picture`), perquè la pàgina que s'ha apartat encara hi és;
- la pàgina torna amb `restore_app`, o sigui amb el `state` que va desar fa un moment i no de zero. Un `raise` que caigui
  entremig es queda amb el vidre i la pàgina apartada no torna.

El preu és **un desament de `state` (1 kB) i un repintat de la pàgina** per cada finestra; l'`apps` d'un llançador no es torna a
escriure perquè no ha canviat (`does not park: the bytes on the disk are the ones it has`). Una app que no és un llançador
també s'aparta —l'`agenda` torna al mes amb la seva forma—, perquè «quan toca» és quan toca per a totes.

**I aquest preu es paga per finestra, o sigui que la freqüència és la meitat del número** (0.190.7, comptat): les dues alarmes
de fons d'aquest arbre repeteixen cada deu minuts cadascuna —la `battery` i la `meteo`, 600 s les dues— i no estan alineades,
o sigui que **una pàgina que no és de l'amo de l'alarma es repinta unes dotze vegades l'hora** mentre la persona la fa servir
(llegint, per exemple). Amb la placa adormida no es paga res: el despert lliura la cua amb el vidre buit i no aparta ningú. Si
un dia el repintat fa nosa, el que s'ha de moure és el pas d'aquestes alarmes de fons o el fet d'apartar una pàgina que algú
està mirant; el que no s'ha de fer és treure l'apartada, que és el que va fer que les mostres existissin.

**I una tanda perduda no obre cap finestra** (0.190.62, l'amo: *«veig que al sortir de sleep manualment passen les dues apps com
si tinguessin alarmes atrassades»*). `take_due` torna a deixar una alarma repetida al seu lloc següent quan la placa s'ha menjat
**períodes sencers**, i el lliurament li obria la finestra igualment: amb la `battery` i la `meteo` a deu minuts, una placa que
ha estat callada una hora passava les dues apps seguides —apartar la pàgina, carregar l'app, llegir el sensor i tornar-la a
obrir— per una mostra que val el mateix d'aquí a deu minuts. Ara una alarma així, que a més **no demana el primer pla** (`raise`
fals), es re-arma **en silenci**: el registre diu `#N for meteo was missed K time(s) while the board was down: it is re-armed and
no page is opened`, i l'app pren la mostra la propera vegada que s'obre —totes dues ho fan a l'arrencar la pàgina—. Una alarma
d'una persona, que porta `raise`, es continua ensenyant encara que la placa hagi estat dies parada. **Que una alarma es perdi és
una altra pregunta**, i la contesta el registre del despert: `power: woke on the timer` o `… on the clock chip` vol dir que la
placa sí que es va llevar a l'hora; una placa que s'ha llevat per la tecla quan la cua tenia una hora és la que cal mirar.

Dins de la finestra: `console.is_foreground()` és `false` i escriure a la consola no fa res (queda al
log). **Cap crida no dona `E_NOTFOREGROUND`**: el codi no el produeix enlloc i la taula d'errors de
`docs/30` el té marcat com a reservat.

Cost a mesurar: cada finestra paga l'arrencada de l'app (crear la VM i carregar l'entry). Per això
una app que vol ser barata en segon pla ha de fer que `on_alarm` no carregui mitja app i ha de tenir
el seu estat al disc (`fs.data`).

**I el que ha sonat es fa dins del ganxo, no al bucle de la pàgina** (0.189.145, l'amo: *«però els avisos arriben a
l'app per fer la recolecció d'informació?»*): una finestra crida `on_alarm` i **destrueix la VM tot seguit**, o sigui
que un ganxo que només deixa una bandera perquè el bucle de la pàgina faci la feina —el que va fer el primer port de la
`Battery`— deixa la feina sense fer quan l'app no és al davant, que és el cas de tota la nit. El ganxo no pot esperar,
però una mostra no espera res: llegeix la placa, l'afegeix a la llista i escriu el fitxer. La segona meitat és la que
és fàcil d'oblidar: **una finestra no ha llegit res**. El bloc de dalt de l'entry només ha corregut fins al guarda
`if not console.is_foreground() then return end`, i per tant el ganxo ha de llegir el registre del dia abans d'afegir-hi
la línia, o el desaria a sobre amb una de sola (docs/30, docs/45, `apps/battery`).

El que el ganxo **no** pot fer és dibuixar: la finestra no té pàgina i el que hi ha al vidre és de l'app del davant.
El bucle només repinta el que el ganxo ha escrit, i el ganxo no hi és per pintar.

### Pujar al davant

Si l'alarma porta `raise = true`, llavors, en comptes d'una finestra que es tanca:

1. L'app de primer pla es tanca net: `on_pause()`, `on_save()`, `on_close()`, i la seva sessió al disc.
2. L'app de l'alarma arrenca **com una pàgina de debò** (`raise_app` → `run_app`), o sigui amb el seu `state` —el d'una
   pàgina que ve d'un son, perquè el bloc és al disc— i sense cap relació amb la VM de cap finestra: **la finestra no hi
   intervé**.
3. I **se li diu què ha sonat**: després del primer torn de la pàgina, `on_alarm(id, payload, meta)` es crida amb la mateixa
   entrada i el mateix `meta.late_ms` que hauria rebut en una finestra (0.190.7).

**El punt 3 és el que faltava, i el text que hi havia aquí deia una cosa que el codi no ha fet mai** (l'havia escrit el
projecte del paper: *«amb la VM que s'estava fent servir a la finestra»*, i la finestra destrueix la seva VM tot seguit). Sense
aquest fil, una alarma amb `raise` és una alarma que **no diu res a l'app**: el ganxo no corria, la pàgina arrencava de zero i
`armed`/`reminded_`/`fired` —el que el ganxo hauria escrit— no hi era. El registre ho diu ara amb una línia:

```
[I][ALARM] 4 raises zzraise
[I][ALARM] 4 told to zzraise in the page it raised (1 ms late)
```

I per això **el payload és obligatori** (0.190.7, l'amo: *«tota alarma porta un payload que se li dona a l'app quan reb
l'alarma»*): en una finestra i en un `raise` l'app no té cap estat propi que recordi què passava, i el payload és l'única
cosa que li arriba. Un `alarm.at` sense `payload` es refusa amb `E_INVAL` (docs/30).

## Alarmes perdudes

Si el sistema estava apagat quan tocava:

- En arrencar, totes les alarmes vençudes es reprodueixen **en ordre**, amb `meta.late_ms` dient quant
  s'han endarrerit i `meta.missed` quantes repeticions s'han perdut. **I es reprodueixen abans que cap pàgina torni**
  (0.189.175): el nucli lliura la cua entre el prompt i el `power::boot` que fa tornar l'app del son, o sigui que una placa que
  desperta per una mostra la pren amb el vidre buit —sense apartar res i sense repintar-lo— i l'app on la persona era hi és
  després.
- Una repetició `"daily"` que s'ha perdut tres dies arriba **una vegada**, no tres.
- Les alarmes que es reprodueixen es tracten com una finestra d'alarma normal, una darrere l'altra — **tret que hagin perdut
  períodes sencers i que no demanin el primer pla** (0.190.62): llavors es tornen a armar **en silenci** i no obren cap pàgina, perquè
  una mostra de la bateria o del temps val el mateix d'aquí a deu minuts i fer passar la pàgina de la persona per ella no és el que
  ningú ha demanat (docs/30, docs/45).

## Repetició

- `repeat = <segons>` o `"daily"` (cada dia a la mateixa hora local).
- Després de disparar-se, es reprograma a la següent ocurrència **encara que l'app hagi fallat**.
- Si `app.raise()` puja l'app, la repetició continua programada.

## Com arrenca la placa

**Abans de tocar el vidre, el sistema sap d'on ve** (0.189.180, l'amo: *«Abans de tocar la pantalla. s'ha de saber si es ve d'un
deepsleep, una arrencada nova o un reinici. Segons d'on es vé es farà un arranc d'una manera o unaltre»*). Són tres i no dues, i el
xip no les sap dir totes: `esp_sleep_get_wakeup_cause()` contesta el mateix —«no he dormit»— a una placa que s'endolla i a una que
s'ha reiniciat. El que les distingeix és **la memòria que només se n'emporta el corrent**: la RAM del RTC, on el nucli deixa quatre
bytes al començament (`LUNK`, docs/70).

| D'on ve | Com ho sap | Què fa l'arrencada |
|---|---|---|
| **Una arrencada nova** (`new`) | la RAM del RTC és buida: la pila ha marxat | les pàgines comencen de zero, la targeta dels moments i la casa |
| **Un reinici** (`restart`) | la RAM del RTC té la marca: el corrent no ha marxat (el botó, un pànic, `reboot`, una pujada de codi) | el mateix que una arrencada nova: és una tirada que comença |
| **Tornar d'un son profund** (`sleep`) | la nota que el son va deixar al magatzem (`power/slept`), que es consumeix amb aquesta arrencada | **el `state` de les pàgines es queda** (§ la sessió `state` de docs/30), **no es torna a pintar la targeta del començament** —el vidre té la imatge del son i el que ve és la pàgina de la persona— i `restore_session` torna l'app on era |

El registre ho diu amb la seva paraula abans de qualsevol pintada:

```
[I][BOOT] a new run: the power came back
[I][BOOT] the chip restarted and the power never went away: a restart of the system
[I][BOOT] back from a deep sleep, woken by the timer of the chip: an alarm, nobody in front, the glass is not touched
```

**I una bateria baixa és l'excepció de la targeta**: una placa que torna d'un son amb la pila sota la marca sí que ensenya el
dibuix, perquè és una cosa que la persona ha de saber i no una targeta de cortesia (docs/30, docs/50). El cicle de l'emulador i
`--persist` són aquestes tres arrencades de mentida en un run de debò (docs/85).

### I el xip també ho sap: el motiu del reset (0.190.68)

L'amo: *«recorda que l'esp32 te estats al arrencar, així el firmware sabrà si ha despertat normal o d'un deep sleep»*. La marca de
la RAM del RTC és **una pista del sistema** —el que la baixada va deixar escrit—, i n'hi ha una segona que és **del maquinari**:
`esp_reset_reason()`, que el xip respon a cada arrencada i que no depèn de cap escriptura que hagi pogut fallar. El nucli la llegeix
com a `hal::ResetCause` (`kPowerOn`, `kDeepSleep`, `kSoftware`, `kPanic`, `kBrownout`, `kExternal`) per
`IPlatform::reset_cause()`, i les dues pistes es creuen en lloc de triar-ne una:

| El xip diu | La marca `power/slept` | Què és aquesta arrencada |
|---|---|---|
| `kDeepSleep` | hi és | **una tornada**, i és el cas net: la placa no ha tallat el corrent (l'ATOM, la T-Koala, la T5, l'emulador) |
| `kDeepSleep` | **no** hi és | **una tornada igualment**, i es diu al registre: l'escriptura de la baixada no va arribar i el maquinari ha salvat el run |
| `kPowerOn` | hi és | **una tornada**: és el cas normal del Paper, que dorm **tallant el corrent** i torna com si l'endollessin (docs/50) |
| `kPowerOn` | no hi és | el corrent ha tornat; si la RAM del RTC té la marca `LUNK`, un reinici sense son |
| `kPanic`, `kBrownout`, `kSoftware`, `kExternal` | el que sigui | la línia del reinici porta el motiu: `the chip restarted and the power never went away: a panic` |

O sigui que **la marca mana quan hi és i el xip mana quan la marca s'ha perdut**, que és exactament el forat que una marca sola
deixava obert: una NVS que no va prendre l'escriptura no és una arrencada nova, perquè el que hi ha al vidre i al disc és el run de
la persona. Quan les dues pistes no diuen el mateix, la que salva el run és la del xip i el registre ho diu:

```
[W][BOOT] the chip came back from a deep sleep and the mark of the sleep is not in the store: this boot is a return anyway
```

El motiu del reset **no substitueix** la marca, però, i això és el que fa que les dues coses siguin necessàries: al Paper el son
profund és un tall de corrent (el seu rellotge de quars torna a engegar la placa), i allà `esp_reset_reason()` contesta `power on`
a una tornada tan bona com qualsevol altra.

**I despertar-se per una alarma no és despertar-se per a una persona** (0.189.182). La placa que torna d'un son perquè el xip
l'ha cridada —`last_wake` contesta `clock` o `timer`— es troba el vidre sense ningú al davant: la pàgina que hi havia espera. Per
aquesta arrencada el sistema, en aquest ordre:

1. lliura la cua d'alarmes (§«Alarmes perdudes»), amb el vidre buit i sense apartar cap pàgina, que és el que fa que una mostra
   es prengui sense haver de repintar res;
2. **no torna la pàgina**: `restore_session` no corre i el registre de la sessió es queda a la NVS, sense consumir;
3. **no pregunta pel pany ni arrenca la casa**: la targeta del `device_lock` i el llançador són per a una persona, i el que ha
   vingut a fer la placa no és atendre-la;
4. i al primer torn, quan el ganxo ja ha corregut, **torna a baixar** sense pintar la imatge del son: el vidre es queda amb el
   que la finestra hi hagi deixat, que és l'avís que una persona trobarà quan vingui.

La pàgina torna amb la propera arrencada que sí que és d'algú: la del botó, la d'un `reboot`, la del cable. És la mateixa nota
del son que fa que el `state` no s'esborri (0.189.176), o sigui que les dues coses són una: *una placa que baixa amb la casa
oberta i torna per una alarma no ha perdut la casa, només l'ha deixada esperant*.

## Son

| Mode | Qui ho decideix | Què es desa |
|---|---|---|
| `light` | L'app (`power.sleep`) | Res: la RAM es manté |
| `deep` | L'app (`power.sleep`) o **el nucli** (la política d'oci, §«Qui arma el despert») | La nota de sessió (quina app hi ha al davant, amb quin fitxer i a quina hora s'ha de despertar) a la NVS, les alarmes persistents, **la marca del son** (`power/slept`) i **el `state` de les apps, que aquesta arrencada no esborra** (0.189.176) |

Abans de dormir, el nucli passa per `on_pause()` i `on_save()` de l'app oberta, i després programa el
despertador amb la primera alarma de la cua. Quan desperta, mira el motiu (`last_wake`) i segueix des
de l'arrencada normal: **la VM no sobreviu a un son profund**, es reconstrueix —i **la forma de la pàgina sí**: la marca que el
son ha deixat al magatzem fa que l'arrencada no esborri el `state` de les apps (0.189.176, l'amo: *«Al tornar d'un deep sleep no
has d'esborrar les sessions state»*), que és el que fa que `restore_session` torni la pàgina tal com era i no de zero. La marca
es consumeix amb aquesta arrencada: la següent, si no ve d'un son, torna a començar les pàgines (docs/30, docs/75).

**Un son lleuger el pot tallar un botó o la propera alarma.** El botó ja hi era; l'alarma és nova:
com que l'alarma és del sistema i no ha de fer cua darrere d'una app que dorm, `kernel::power` baixa
el termini del son fins a l'alarma més propera. La promesa es tanca igual (`true`), i l'app que
necessiti el descans sencer l'ha de tornar a demanar.

**I la casa no és ningú treballant** (0.189.182, l'amo: *«Perqué no ha despertat la placa? estava en deep sleep i ho hauría
d'haver fet»*). La política d'oci es negava a dormir mentre hi hagués **qualsevol** app al davant, i la placa d'aquest sistema
en té una sempre que `cfg home` nomena una app: o sigui que una placa amb llançador no dormia mai, la seva alarma no trobava cap
son que esperar i la mostra de les deu minuts no es prenia. Ara la pàgina de casa —i **només** la de casa, la que respon a
`cfg home`— és com el prompt: es tanca amb els seus comiats i la seva sessió, com qualsevol altra pàgina, i la placa se'n va. Una
app que no és la casa continua aturant la política, que és el que vol dir «algú hi és».

**Un son profund no el talla res.** Si una app en demana un de més llarg que la propera alarma, la
placa dorm fins al seu termini i l'alarma s'entrega tard (`late_ms` ho diu): el son que ha demanat una app és una decisió que
la placa respecta, i no una promesa que el sistema pugui trencar pel seu compte. El que el sistema sí que fa des de la
0.189.182 és el camí propi —baixar sol fins a la propera alarma i tornar només per a la finestra—, que és el que passa quan no
hi ha ningú a la placa.

**Les dues fondàries hi són a les dues bandes**, i la placa ho diu: `power.caps().sleep_light` i
`power.caps().sleep_deep` són certs, i `power.caps().wake` diu de què es pot despertar (`"timer,
button"` a l'ATOM, `"timer, clock, button"` al Paper —l'hora del xip i la tecla PWR del dial—). I el sistema
**sí que es dorm sol** al Paper, quan hi ha una alarma pendent i ningú no fa servir la placa (§«Qui arma el
despert»); a les plaques sense xip de rellotge no, perquè un caràcter que arribi mentre dorm es perd i no hi ha
pantalla per dir-ho.

**I el son del Paper té les dues portes** (0.190.11, l'amo: *«posa el deep sleep amb alarma… Molt bé, però has
tret l'int dels botons físics!!! Vull ambdues opcions»*): el son profund amb hora armava el rellotge del xip i
**no** la tecla, perquè aquesta placa construïa el seu objecte de potència sense cap pin de despertar (`Esp32Power(-1)`
des de la 0.7.0). Ara arma les dues coses —`ext0` sobre el GPIO38, que és la tecla PWR del dial, i l'alarma de la
BM8563—, o sigui que la placa torna a l'hora que li van demanar **i** a la tecla d'una persona. Amb la bateria el
corrent se'n va abans i la tecla PWR és la del xip de potència, que és com aquesta placa s'ha encès sempre (docs/50).

## La velocitat del processador: 160 de repòs i dues finestres (0.179.38)

**La CPU és la mateixa decisió que el son** —energia— i es pren al nucli i no dins d'una app: la placa diu quins passos té
(80, 160 i 240) i un altre número es refusa en comptes d'arrodonir-se.

**Des de la 0.190.175 el rellotge el mou el `esp_pm` del xip** (l'amo: *«la cpu es pot escalar sola a esp_idf... activa la opció i
adapta el que tenim fet manualment d'scaling»*). El que aquest sistema diu no és un pas sinó **quan vol el sostre**, i el xip va del terra
al sostre pel seu compte de càrrega entre dos locks:

| Moment | Velocitat | Qui ho diu |
|---|---|---|
| Arrencada i començament d'una app | **el sostre del rang** (240 de sèrie) | `power::hold_cpu(kBusyMhz, …)` → el lock `ESP_PM_CPU_FREQ_MAX` |
| Una imatge, una font, un epub | **el sostre**, mentre duri la feina | `power::work_cpu(…)`, el mateix lock |
| Enviar al panell, esperar la tinta, escriure a la targeta | **el terra del rang** (80 de sèrie) | `power::wait_cpu(…)`, el lock deixat anar |
| La resta | **el que el xip triï** entre el terra i el sostre | `esp_pm`, per la càrrega dels tasks |
| Amb l'escala apagada | **`Speed:` i prou** | `esp_pm_configure({min = max = Speed})` |

**El terra i el sostre són el que la persona permet**: `Allow 80 MHz` i `Allow 240 MHz` a Settings→CPU (el del mig va marxar a la
0.190.175: el xip decideix les freqüències d'entremig, i una que no es pot permetre ni prohibir no és cap ajust).

- **Dues portes i no una: la feina i l'espera** (0.190.26, l'amo: *«Si està habilitat el Automatic scaling: al carregar una imatge,
  puja a 240; al carregar un epub, 240; al carregar una font, 240; al enviar dades al eink usa 80»*). `power::work_cpu` és el que el
  xip té **a calcular** —descodificar una imatge, convertir una font, el `layout` d'una pàgina d'epub i els glifs de cada línia, que
  és el que fa car un llibre de debò— i `power::wait_cpu` és el que el xip té **a esperar**: el bus del panell, la tinta, la targeta.
  La diferència no és de grau sinó de naturalesa, i per això són dues funcions i no un número que cada crida endevina. **Les dues
  només manen amb l'escalat automàtic encès**: qui ha fixat un pas a mà el vol fixat, i aquesta és la promesa de `power cpu`.
- **I l'enviament al panell baixa a 80 encara que el `gather` sigui feina**: les files que es llegeixen de la imatge costen unes
  tres vegades més a 80 (99 ms contra ~300 en una pàgina), però el panell triga 783 ms a tinta igualment i el corrent del pas de
  dalt no el paga ningú. El número de debò és a `docs/95` (D-5).

- **La finestra és un comptador del xip i no un rellotge del nucli** (0.190.175): `hold_cpu(mhz, ms, why)` agafa o deixa anar el
  lock `ESP_PM_CPU_FREQ_MAX`, i el xip compta els que el tenen. **El `ms` ja no fa res**: una finestra que havia de caducar en un
  torn del nucli era un número que havia de ser prou gran per no baixar a mig decode i prou petit per no quedar-se a dalt per res,
  i el que el tanca ara és el `rest_cpu` o el `wait_cpu` que ve després (docs/50).
- **El que demana algú mana sobre el repòs**: `power cpu <mhz>` a la consola i `power.cpu(mhz)` de Lua fixen **el terra i el sostre
  alhora** (un rang d'un sol valor) i apaguen l'escala automàtica. Així una persona que baixa la placa a 80 per mesurar no es troba
  que la propera app la torna a pujar.
- **I hi ha plaques on no hi ha res a triar** (0.190.227, l'amo: *«Desactiva el Dynamic Frequency Scaling per l'M5 Atom Lite i l'M5 Atom
  Matrix. Només per aquests dos models. Que vagin sempre a 240Mhz»*): el Lite i el Matrix diuen `board::scales_the_clock() == false` i el nucli
  els deixa el rang en un sol número, 240, amb **el son lleuger intacte** —el xip dorm entre torns igual i el que no fa és córrer a mitja
  marxa—. `power cpu 160` i `power auto on` hi responen que aquesta placa no escala i no canvien res; el que hi hagi desat de velocitat i de
  política **no es llegeix i no s'esborra** (docs/50).
- **Els canvis es veuen al registre**, tots i cada un, amb el número que hi havia abans i el motiu:
  `[I][POWER] cpu 240 MHz from 160 (an app is starting)`, i el tancament de la finestra
  `[I][POWER] cpu 160 MHz from 240 (the load is over)`. Una velocitat que el xip no accepta surt com a avís i diu a quina
  corre. **La línia del boot hi és encara que el xip ja arrenqui a 240**: la política és del sistema i no una casualitat del
  valor per defecte.
- **Per què 160 i no 80**: per sota de 80 l'APB baixa amb la CPU i cap perifèric del core es reconfigura (docs/30), o sigui
  que el port sèrie sortiria amb el baud malament. 160 és el repòs de debò d'aquesta placa, i les dues finestres són els
  moments en què el treball és, per endavant, pesat: muntar el sistema i compilar i arrencar un intèrpret de Lua.
- **`load`: la càrrega en tres paraules** (0.189.93, l'amo: *«fes una comanda que doni el valor low, normal o high i usarem
  aquest valor per ajustar les 3 velocitats de la CPU 80, 160, 240»*). És **el que el nucli treballa menys el que espera** dins
  d'una finestra de 250 ms —`kernel.loop` menys `queue.wait`—, perquè una placa que passa la vida a `queue.wait` està parada amb
  el comptador en marxa i la seva càrrega és zero encara que faci milers de torns (el primer esborrany de la línia deia
  noranta-un per cent en una pàgina que no feia res). Contesta `low` per sota del 20 %, `normal` entre el 20 i el 60 i `high`
  per sobre del 60, **amb cinc punts de banda a cada llindar** perquè un rellotge que es mou a cada torn costa més que el que
  estalvia, i el percentatge va al costat de la paraula (`low (2.7 %)`) perquè una paraula sense número no es pot afinar.
  `power::load_of()` és la decisió sola i pura, i la suite del shell la subjecta sense placa.
- **I la tendència del minut és la que mou la CPU, sola** (0.189.95, l'amo: *«no facis el canvi de freqüència tant sovint...
  Guarda mostres, durant un minut: si la tendència és a més, puja la CPU; si és a menys, la baixes; si és semblant, et quedes
  com estàs»*). **`low` és 80 MHz, `normal` 160 i `high` 240**, i el rellotge no es mou per una lectura sinó per una
  **tendència**: una mostra cada dos segons —trenta, trenta bytes— i la mitjana de les **quinze últimes contra les quinze
  d'abans**. Si la mitjana ha pujat deu punts o més, **un pas amunt** (80→160, 160→240); si ha baixat deu punts, **un pas
  avall**; si no, **res**. Mai dos passos al mateix torn: el minut següent és el que diu si el primer va valer la pena.
  - **Una decisió per mostra i no per torn** (0.189.96): the clock can move at most once every two seconds however many turns
    the board takes in between, and **el tancament d'una finestra no mou res**: a window is a loan of the top step while the
    work lasts and the governor is what decides after it. Abans, cada paint feia `240→160` en tancar la finestra i `160→240`
    al paint següent, i el log d'una pàgina que es repinta era una llista d'anades i vingudes (l'amo, amb el log al davant:
    *«és massa ràpid»*). **I un pas que ja és el que hi ha no es diu**: mig log era `cpu 240 MHz from 240 (a paint)`.
  - **Si la tendència és plana, mana el nivell de la mitja part de minut**: per sota del deu per cent la placa està parada i
    baixa al pas de baix en un sol moviment, per sobre del cinquanta està treballant i puja al de dalt; entremig, no es toca.
    La tendència sempre mana sobre el nivell.
  - Una finestra mana igual que abans (boot, arrencada d'una app, paint), i `power auto on` comença un minut nou.
  - `load` segueix contestant la **lectura** de la finestra de 250 ms (i el seu percentatge), que és el que es mira per
    ajustar els llindars; el governador mira la sèrie.
  - **Una finestra mana sobre el governador**: el boot, l'arrencada d'una app i **un paint** demanen 240 per endavant
    (`hold_cpu`), i baixar la velocitat al mig d'una d'aquestes coses és fer-les més lentes per estalviar el que no es
    gastava. Quan la finestra es tanca, el governador és qui diu el pas de tornada.
  - **`power cpu <mhz>` para la política** i ho diu a la línia del registre (*«asked, the automatic scaling is off»*): qui
    fixa un pas ho fa per mesurar alguna cosa, i un governador que ho desfés dos-cents cinquanta mil·lisegons després no es
    podria mesurar mai. **`power auto`** diu si és encesa i **`power auto on|off`** la torna a encendre, i la torna a
    encendre posant el board al pas que diu la càrrega en la mateixa línia.
  - **La baixada és el que s'estalvia**: la placa que no fa res baixa a 80 en comptes de quedar-se a 160, que és on la deixava
    la política de les dues finestres (docs/95).

## La consola del cable mana sobre la son (0.190.184)

L'amo: *«La shell del lunokOS te un temporitzador d'inactivitat. Si fa 5 minuts que no teclejes res o es reb alguna cosa la sessió es tanca […]
En aquest moment s'allibera el pany o el que sigui perquè idf pugui seguir fent lightsleeps»*, i *«es podia parar el lightsleep simplement amb un
mutex o algo així oi?»*. Sí, i és el lock `ESP_PM_NO_LIGHT_SLEEP` de la gestió de potència: `IPower::hold_no_light_sleep`, amb comptador, agafat
mentre la consola del cable té una sessió oberta i deixat anar quan es tanca.

- **Per què calia**: un son lleuger atura els rellotges i el despertar per l'UART és lent, o sigui que **els caràcters que arriben mentre el xip
  torna es perden** i una tecla «no arriba». I a sobre el registre s'omple de `5085 light sleep(s), 195673 ms in all`.
- **El parany, i com es resol**: una sessió oberta vol dir un xip que no dorm mai, i el prompt existeix des de l'arrencada. Per això **el cable
  arrenca amb la sessió tancada** —`the session is closed: press Enter to open one`— i el xip fa becaines de seguida; qui vol el prompt prem
  Enter, i el pany es torna a agafar.
- **El temporitzador és de cinc minuts** (`kConsoleSessionIdleMs`) i el mira el torn (`shell::poll_idle`). Una app al davant no compta com a
  inactivitat. Al cap dels cinc minuts la sessió es tanca amb la línia de sempre i el lock es deixa anar.
- **Només al cable**: `IConsole::virtual_terminal()` és cert a l'emulador i a la consola del vidre, que guarden els bytes en una cua i no en
  perden cap. O sigui que el pany no s'hi agafa mai, i cap test no ha de començar prement Enter.
- **I el gate de deu segons es queda** (`console_gate_ms`): és una altra cosa —el pany **del torn**, que decideix si una espera concreta es passa
  dormint— i el lock és el de la sessió sencera.
- **El registre ja no surt pel cable** (0.190.185, l'amo: *«fes que el log no s'envii al serie. per això está la comanda log»*): el mirall del
  registre s'obre sol només on la consola no és el cable, i al port les línies es demanen amb `log` (el cercle) o amb `log follow` (en directe,
  fins a Control+C o un sol Escape). És la mateixa decisió que el pany, presa amb el mateix predicat, i el motiu és el de sempre: **el cable és
  d'una persona** i una traça al mig del que escriu és el que li fa perdre el que estava dient. Una línia `[E]` sí que hi arriba sempre; una
  placa que no arriba a pintar el prompt no té cap `log` a qui preguntar.

## Les esperes es passen dormint (0.190.63)

L'amo: *«[X] Light sleep instead of wait. Aplica lightSleep allá on puguis»*. El sistema passa mitja vida esperant, i esperar amb el
processador despert és el corrent més fàcil d'estalviar d'aquesta placa. Amb el commutador **Light sleep instead of wait** de la
pestanya Battery encès —`cfg.nap on|off`, `cfg.nap()`, i ve de sèrie—, l'espera del **torn del nucli** (`kernel::poll`, el que
arribi a la cua fins a un segon quan no hi ha cap app) és un **son lleuger**, i el que el desfà és una tecla, el dit, la consola o el
temporitzador.

**I des de la 0.190.178 el xip dorma sol** (l'amo: *«activem el lightsleep automàtic d'idf»*): la dormida no la demana el nucli sinó la
gestió de potència del xip, que atura el processador quan el planificador queda sense feina. **El que continua sent d'aquest sistema és la
pregunta**: `esp_pm_register_skip_light_sleep_callback` crida el pany abans de cada son automàtic —`the_doors_are_shut()` a la placa—, i
el pany només és obert quan `power::open_the_sleep` ha fet totes les preguntes de sempre i ha armat les portes de la placa. O sigui que la
quietud la continua decidint `kernel::power` i el que canvia és qui fa la dormida: la IDF, que mira el proper temporitzador i **compensa el
tick** que el son s'ha menjat (un `esp_light_sleep_start` cridat a mà el deixa enrere).

**I el pany protegeix de franc el que abans s'havia de protegir a mà**: la tinta d'una pintada, una consola que encara no ha callat i un port
cedit a una app esperen **fora del torn**, on el pany és tancat, o sigui que no hi ha cap d'ells que hagi de saber que el son existeix.

**I només dorma al torn** (0.190.69): the ink of the glass (`splash::settle`) and the card's margin (`power::settle_before_leaving`) are
awake waits, and now they are because the pany is shut and not because nobody sleeps there. Those are the last seconds before the current
goes and a wait with a frame or a write in flight, and the night of the 20/11 the Paper hung on its way down with exactly one of them
behind it. El torn del nucli és on la placa passa l'estona, on no hi ha cap transacció a mitges i on una becaina que surt malament costa un
torn (docs/50, docs/75).

- **El son és lleuger i no un reset**: la RAM, l'intèrpret, la pàgina i el `state` hi són en tornar, o sigui que no s'ha de desar res ni
  tornar a obrir cap app; el que s'atura són els rellotges.
- **I l'escala de la becaina ja no hi és** (0.190.181, l'amo veient el registre de l'ATOM: *«No haviem quedat que aixó era cosa del
  pasat? […] Que ara esp_idf gestionava el lightSleep?»*). La 0.190.63 va néixer amb una escala —quaranta mil·lisegons i doblar fins a un
  segon— perquè llavors era el nucli qui demanava la dormida i n'havia de triar la durada. Des de la 0.190.178 la dormida la fa la gestió
  de potència dins del `pop`, i la durada la donen **el `wait` del torn** i el proper temporitzador que la IDF mira: l'escala no decidia
  res —`min(wait, escala)` donava sempre el `wait`— i continuava pujant. El registre de l'amo ho va ensenyar en dues línies seguides:
  `163 light sleep(s), 5026 ms in all` (trenta mil·lisegons cada una) i, a sota, `the nap is at its ceiling: 1000 ms a turn`. **La
  segona era una mentida** i la línia del sostre ha marxat amb l'escala.
- **La mà mana sobre la becaina, i el topall de deu mil·lisegons es queda** (0.190.71). La 0.190.70 el va treure donant per fet que la
  tecla i el dit despertarien el son lleuger; **al Paper no ho van fer** i la placa va quedar a un segon per torn amb el tàctil llegit un
  cop per segon. Una placa que no pot respondre a un dit no pot dormir.
- **El tàctil no moria de latència: moria perquè la becaina li girava la línia** (0.190.74). `gpio_wakeup_enable()` escriu el **`int_type`
  del pin** —l'arma pel nivell— i `gpio_wakeup_disable()` no el torna enrere, o sigui que la vora de baixada del GT911 quedava convertida en
  una interrupció de nivell. Era el mateix a un segon (0.190.70) que a quaranta mil·lisegons (0.190.72): no era l'espera, era la línia. Ara
  cada línia armada es desa amb el seu tipus i es torna a posar en sortir, i després d'una becaina el panell es torna a armar
  (`ITouch::rearm`). El torn espera **quaranta mil·lisegons** dormint, que és el que un dit no nota.
- **I per això hi ha `power nap [ms]`**: demana un son lleuger d'un segon i diu què l'ha tornat (`a nap of 987 ms ended by a key or the
  glass`, o que el xip no l'ha pres). És l'instrument que diu si el vidre d'aquesta placa és una porta del son lleuger, que és el que
  decideix si el torn pot esperar-lo. I una tanda de becaines **es diu sola cada deu segons de son** (`250 nap(s), 41000 ms asleep, and
  nothing has happened`), perquè la línia que només sortia quan una mà la tancava no la veia mai una placa quieta (docs/50, docs/95).
- **Les portes són les de la placa** i les decideix `IPower::can_nap()`: al Paper, la tecla del dial, **la línia del GT911** i la
  consola; als ATOM i a la T-Koala, la tecla i la consola; a la T5 E-Paper S3 Pro **no**, perquè la seva consola és el USB del xip i un
  son lleuger l'aturaria; a l'emulador tampoc, perquè el PC no dorm (docs/50).
- **La consola és la porta que no es pot perdre**: si el xip no la pot prendre (`esp_sleep_enable_uart_wakeup`), no es dorm. Una
  becaina que es menja el que una persona escriu és pitjor que no estalviar res.
- **I amb una persona a la consola no es dorm** (0.190.75, l'amo: *«El port sèrie és inutilitzable si fa LightSleeps… Si el port sèrie està en
  ús no usar lightSleep»*). El primer byte desperta el xip, però els que arriben mentre es lleva es perden i una línia escrita a mà surt
  tallada: la becaina només es demana amb **deu segons de silenci** a la línia i amb el port no cedit a cap app (`serial::claimed()`).
- **I amb un dit damunt del vidre no es dorma, passi el que passi** (0.190.87, l'amo: *«mentre el dit toqui la pantalla també has de
  desactivar temporalment el LightSleep per evitar tenir problemes amb les lectures fins que el dit aixequi»*). Una porta armada només
  diu que el xip es llevarà: el marc del tàctil arriba igualment quan el xip es lleva, i una lectura que cau dins del son es perd —i una
  lectura perduda a sobre d'un dit és un toc que no arriba o que arriba dos cops—. `touch::poll` diu a `power` que hi ha un contacte
  (`power::note_a_finger`) i la becaina calla fins que el vidre ha estat un escaneig sencer sense ningú (`note_a_finger(false)`), que és
  el mateix senyal que aixeca el contacte. Val amb el Light sleep engegat i amb el DeepSleep polsat d'un app: el que mana és el dit
  (docs/30, docs/50).
- **I el dit també mana sobre la velocitat** (0.190.87): mentre hi és, el governador no hi té res a dir i la placa treballa al cim —un
  dit quiet en un panell que només avisa dels canvis sembla una placa sense feina, i baixar-li la velocitat és fer-li esperar la pàgina
  just quan l'està esperant—. Amb l'escala apagada no es mou res: mana el pas que la persona ha triat (docs/50).
- **I només s'arma una porta que ara és tancada** (0.190.69, el registre de l'amo del 20/11: *«the chip did not take the nap (259) … I
  es queda penjada»*). El 259 era `ESP_ERR_SLEEP_REJECT`: una porta de nivell que ja és baixa fa que el xip **refusi** la becaina
  sencera, i la línia del GT911 queda baixa mentre hi ha un contacte —o sigui que la becaina es refusava una vegada i una altra sense
  dormir mai—. Ara es mira el nivell abans d'armar (`gpio_get_level`), el resultat el diu `esp_sleep_get_wakeup_cause()` i el temps
  viscut —el número que torna `esp_light_sleep_start()` **no** és un `ESP_OK`, és el motiu del despert—, i una placa que ha dit que no
  tres vegades deixa de preguntar dient-ho una vegada: `no more light sleeps in this run: the waits are spent awake` (docs/50, docs/95).
- **I cap armat no sobreviu a la becaina**: en sortir es desfan totes les portes i el compte del temporitzador, perquè el son profund
  arma el que vol i no ha d'heretar res (docs/50).
- **I al registre es veu, i des de la 0.190.74 es veu sol**: la línia de l'arrencada diu si la placa dorma, `a nap of 987 ms ended by a key
  or the glass` diu quant ha durat i què l'ha tallat, i cada **cinc segons de son** surt `250 nap(s), 10000 ms asleep, and nothing has
  happened` — el que abans només es deia quan una mà tancava la tanda, i una placa quieta no la tancava mai (l'amo: *«VULL SABER QUAN HI HA
  LIGHTSLEEP AL SISTEMA I EL MOTIU»*). Tres línies i cap d'elles per torn, perquè una becaina per segon ompliria el registre del sistema:

  ```
  [I][POWER] light sleep instead of wait: the waits are spent asleep, and a key, the glass or the console wakes the chip
  [D][POWER] a nap of 37 ms ended by the console
  [D][POWER] 12 nap(s) in a row, 4200 ms asleep, and now something has happened
  ```

  la primera és **una per arrencada** i diu si la placa fa becaines (o que el commutador és encès i la placa no en pren cap); la segona
  surt **només quan una becaina s'acaba abans del seu temps** —una tecla, el dit o la consola— i diu quina porta ha estat i quants
  mil·lisegons ha durat, que és el que explica un toc que sembla lent; i la tercera **tanca una tanda** quan una mà mou la placa, amb
  quantes becaines seguides i quants mil·lisegons adormida (per sota d'un segon no es diu). Quan l'escala arriba al sostre surt una
  línia sola: `the nap is at its ceiling: 1000 ms a turn while nothing happens`.

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
  **I una persona que torna no és un tall** (0.190.67): una tecla o un dit al vidre és la porta que la
  placa té per a una persona i no un defecte, o sigui que `kOther` i `kTouch` diuen
  `a button/glass woke the board, N ms before its hour, and nothing is measured` i **no apunten res a
  `power last`**, perquè el rellotge no havia de provar res. El registre de l'emulador del 20/11 amb el
  xip adormit de debò és el que ho va ensenyar: `the sleep was cut short by the glass, 69347 ms before
  the hour it was given` per una placa que havia fet exactament el que se li havia demanat.
- **L'arrencada no és part de l'error del rellotge** (`hal::wake_error_ms`, 31/10). Una placa que es
  reinicia no informa en el moment de despertar-se: abans hi ha el carregador d'arrencada, el
  banner, la flash, la targeta i els ajustos, i això és **al voltant d'un segon de firmware**. Al
  Paper, un son de 20 s va sortir `+1 191 ms` i `+1 236 ms` en dues tandes, que és un **6 %** i no ho
  és: és el firmware comptant-se a si mateix. El nucli ara **resta el temps des de l'arrencada**
  (`monotonic_ms`) quan el despert ve d'un son de debò, i deixa el número net. El que queda dins és
  el carregador d'arrencada, que es mesura a part (`docs/95`, E-3).
- **Un número negatiu no és un error: és el RC del xip** (19/11). El despert d'un son que no apaga el
  corrent el compta el **temporitzador del SoC**, i el seu rellotge lento és l'oscil·lador RC de la
  placa —no un quars—, que és del mig per cent. Al log del 19/11: `woke on the timer, -2182 ms` en un
  son de 493 s és un **−0,44 %**, i el que sembla una contradicció amb el `#1 fired … 1689 ms late`
  no ho és: **−2 182 + 3 871 d'arrencada = +1 689**. O sigui que la placa va despertar 2,2 s abans
  d'hora (el RC) i l'alarma es va lliurar 1,7 s tard perquè l'arrencada costa gairebé quatre segons.
  El quars el té el xip del rellotge, i amb la bateria és ell qui compta (`(clock)` a `power last`);
  amb el cable, el corrent no se'n va i el que compta és l'RC (docs/50, docs/95).

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
   Els números de la política, que són decisions i no mesures: **la quietud que tria la persona** a *Sleep after*
   —cinc, deu o quinze minuts des de la 0.190.13—, una espera mínima de **vint segons** (era de deu fins a la 0.190.20:
   la baixada sola en gasta més) i cap app ni `keep_awake` enmig. `power idle off` la desactiva mentre una persona
   treballa al prompt.

   **I la pregunta era la que no tocava** (0.190.272, l'amo: *«l'M5Paper funciona genial i s'apaga/deepsleep, aquesta
   versió no sembla fer-ho bé. Tampoc sembla que els temportizadors d'apagar backlight i entrar en deepSleep quan hi ha
   inactivitat»*). `idle_sleep` començava mirant `deep_powers_off()`, que diu **com** baixa la placa —una apagada de debò,
   amb el rellotge del xip tornant-la a engegar, que és el Paper—, i la T5 E-Paper S3 Pro hi contestava que no perquè el seu
   son profund és un son de l'ESP32: **la seva política no dormia mai**, i ni `Sleep after` ni el respir de la llum tenien
   res a fer-hi. Ara la pregunta és `can_sleep_on_idle()` —«aquesta placa estalvia si baixa?»—, que per defecte val el mateix
   que abans i que la T5 contesta que sí; el que decideix després és el que ja hi era: la porta per tornar-ne.
   **I la resposta de la T5 es va quedar escrita i no pas posada** (0.190.293, l'amo: *«la placa T5S3 encara no es posa a dormir
   quan toca amb el timeout»*): aquesta mateixa frase deia que la T5 contesta que sí i la classe `T5Power` no tenia
   l'`override` de `can_sleep_on_idle`, o sigui que la resposta de sèrie —la de `deep_powers_off`, falsa en aquesta banda— era
   la que arribava i el temporitzador no disparava res. Ara hi és, i és `true`: baixar estalvia (llum a zero, carrils del TPS65185
   avall, vidre descansat i xip aturat) i les portes de tornada són tres (el BOOT, el rellotge del xip i el dit). **I el mateix
   error tenia la llum**: `Dim when idle` demanava que el cable no estigués carregant, o sigui que a la T5 —que sap dir-ho pel
   BQ25896— el vidre no s'apagava mai amb el cable a dins; a la M5Paper no es veia perquè el seu `battery()` no pot dir si
   carrega. Ara el comptador de la quietud mana sobre el respir, i el respir s'atura amb el negre (docs/50).

   **I "ningú l'està fent servir" era només la consola** (0.190.5, l'amo: *«sembla que s'adorm sola quasi
   d'inmediat... perqué?»*). El comptador del silenci el movien les tecles del terminal i prou: un dit al
   vidre —la targeta del sistema, una pàgina del launcher, la banda del teclat— i una tecla del dial no
   comptaven, o sigui que la placa s'apagava sota la mà que la feia servir. Ara **un marc amb un dit a
   sobre i una tecla que baixa són la placa en ús** (`power::note_activity`, el mateix avís que ja rebia
   el pany, 0.189.150), i **la targeta del sistema desperta la placa mentre és fora**: mentre és oberta
   l'app ni tan sols corre (`kernel.cpp`), o sigui que allà la placa és del sistema i una persona l'està
   llegint; apagar-s'hi s'endú la targeta i pinta la imatge del son damunt del que la persona acabava
   d'obrir (docs/30, docs/50, docs/90).

   **I es pot apagar des de Settings** (0.190.5): *Sleep when idle*, a la pestanya **Battery** del panell d'ajustos (`cfg.sleep`, o
   `cfg sleep on|off` al prompt). És un ajust i no una llei perquè hi ha tardes en què una persona vol la placa desperta —una demo,
   una sessió de mesura, una app que escriu un registre—, la resposta es desa i el boot la torna a llegir; `power idle off` continua
   sent l'interruptor del run. **I el comptador de la quietud arrenca a zero a cada boot** i el mouen les tres coses que són una mà a la
   placa: un byte de consola, un marc amb un dit a sobre i una tecla del dial que baixa (docs/30, docs/90).

   **I el comptador és UN** (0.190.9, l'amo: *«el comptador d'idle es el mateix que per el que fa saltar el device lock… No
   malgastis temps i memoria en varis comptadors»*): el son profund i el pany de la pantalla preguntaven la mateixa cosa amb dues
   xifres diferents, i ara hi ha `power::quiet_ms()`, que és el que llegeixen tots dos —i el que una app llegeix amb
   `sys.idle_ms()`—. Les tres regles són les de l'amo i són al mateix lloc: **una arrencada reinicia el comptador**, **una tecla
   física el reinicia** i **un dit al vidre el reinicia**, i **la placa només s'apaga quan la quietud arriba al temps triat al
   desplegable**: amb cinc minuts, cinc minuts, i no els deu segons que hi havia. **Els tres temps són 5, 10 i 15 minuts**
   (0.190.13, l'amo: *«Rectifica els temps del DeepSleep, posa de 5,10,15 minuts»*), els mateixos del pany, o sigui que una persona
   pot triar que el pany arribi abans que el son —5 de pany i 15 de son—, que és l'ordre que té sentit. L'única cosa que no espera el comptador és una
   represa per una alarma, que ha fet la seva feina i torna a baixar de seguida (0.189.182).

   **I quan la política decideix no dormir, ho diu** (0.190.12, l'amo: *«No aconsegueixo que saltin. Revisa el codi que fa
   l'activació i com es suma temps al user_idle»*): una línia, una vegada per motiu i com a molt un cop al minut, amb el número de
   quietud que hi ha i el que demana la política —`idle: 300 s of quiet (the policy asks 300) and the board stays awake: an app is
   in front…`—. Els motius són els cinc de la llista de sobre, amb el seu nom: una app al davant o que ha demanat quedar-se despert,
   la targeta del sistema fora, la llum amb un quadre per pintar, la placa sense hora i l'alarma massa a prop perquè valgui un
   arrencada. Sense aquesta línia, un comptador que creix i una condició que falla es veuen exactament igual (docs/30, docs/95).

   **I les condicions que no poden canviar no compten res** (0.190.88, l'amo: *«si la opció no està activada, perqué molestes amb
   missatges irrellevants?»*). El commutador apagat i un vidre on un son profund no és una baixada són fets del run i no pas
   condicions que es puguin complir més tard: en aquests dos casos la línia deixa el compte enrere, **baixa a `debug`** i diu el fet
   una vegada —`idle: this board is not put down by the policy: this board's deep sleep is not a power off`—. El que no pot passar
   no és una notícia: és la resposta a qui pregunti, i el registre normal no la torna a veure. El compte enrere es queda per als
   cinc motius que sí que canvien, que és on el número vol dir alguna cosa; i la frase repetida cada minut en una placa que no es pot
   baixar era exactament el soroll que l'amo va assenyalar (docs/30, docs/95).

   **I sense cap alarma la porta és la tecla** (0.190.12): la política no feia res quan la cua era buida —un `return` sense línia—,
   o sigui que una placa sense alarma no s'adormia mai i el commutador de Battery no feia res. Ara baixa **fins a la tecla**: la
   sessió es desa, la imatge del son es pinta i la placa torna on era quan algú prem el dial. **Amb una alarma, les dues portes**:
   l'hora del xip i la tecla (docs/50, docs/90).

   **I aquest camí és el del sistema i no el del xip** (0.190.14): la política va per `shell::deep_sleep` —el mateix que el botó
   de la lluna del panell i el `deepsleep` del prompt—, que és qui **acaba la run** quan el que baixa és un emulador
   (`kernel::shutdown`). Amb la crida directa al xip l'emulador es quedava amb la imatge del son al vidre, l'app tancada i el
   procés viu, i la potència de mentida fins i tot deia *«the emulator ends»* mentre no s'acabava.

   **I hi ha una baixada i prou** (0.190.15, l'amo: *«el codi d'apagada per deepsleep ha de ser el mateix a tot arreu, tant si salta
   per timeout d'inactivitat com si l'usuari l'activa manualment… Veig que si inicio sleep manual no es setegen les alarmes?»*). Tres
   coses són úniques i no tres:

   - **la preparació** (`power::sleep`): la sessió de l'app que hi ha al davant, la marca del son, el disc amb les sessions aparcades i
     el repòs de la targeta, la imatge de la baixada, el buidatge de la línia i la llista de rellotges;
   - **l'hora** (`the_hour_of_a_sleep`): **la més propera** entre la que s'ha demanat i la propera alarma, i **la propera alarma quan
     no se n'ha demanat cap** (zero, que és el que demanen el botó de la lluna, el `deepsleep` i `sys.deepsleep`). Zero no és una
     hora: és la tecla;
   - **la crida al HAL** (`IPower::sleep`): `sleep_with_the_keys` desapareix perquè era **una altra baixada** —al Paper desarmava el
     xip del rellotge, o sigui que una son demanada per una persona s'enduia les alarmes—, i ara una placa amb tecla dorm amb
     `sleep(kDeep, 0)`. On hi ha les dues portes s'armen **les dues**: el temporitzador del xip i la tecla del dial, i el xip del
     rellotge quan és el corrent qui ha de tornar. La línia del registre és la mateixa a tota baixada (`say_the_way_down`, docs/50).

   O sigui que una persona que fa dormir la placa amb una alarma armada **torna a l'hora dita i amb el dit**, i la política que la fa
   dormir amb la cua buida torna amb el dit. El cas `test_a_manual_deep_sleep_arms_the_nearest_alarm` ho aguanta: amb el camí de les
   tecles d'abans l'alarma es quedava a la cua.

   **I les portes d'un son profund són tres** (0.190.16, l'amo: *«del DeepSleep s'ha de poder tornar sempre a part de per les alarmes,
   també els botons físics i opcionalment el panell tàctil (per defecte false) així no cal mantenir despert el panell»*):

   - **l'hora**, si n'hi ha una: el temporitzador del xip, i el xip del rellotge quan el corrent se'n va de debò;
   - **la tecla del dial (GPIO38)**, sempre: `ext0` sobre el pin, que és del domini RTC i que la placa ja fa servir per engegar-se;
   - **el dit**, i només si una persona ho ha demanat (`cfg.touchwake on`, la fila *Wake on touch* de la pestanya Battery, `cfg.touch_wake()`
     des de Lua). **Apagat de sèrie**: sentir un dit vol dir deixar encesos el rail del panell i el controlador del tàctil tota la son, i
     això és el corrent que el son existeix per estalviar. Amb el commutador encès, la baixada **no talla el rail del panell** (el GT911
     hi viu), deixa el vidre reposat, i arma `ext1` sobre el **GPIO36** amb la condició «aquest pin baix» —el GT911 porta la línia avall
     quan un escombrat té un marc—; `ext1` d'un ESP32 només sap «tots baixos» o «qualsevol alt», i amb un sol pin «tots baixos» és
     exactament «aquest baix» (docs/50, docs/95). **I la línia no s'arma fins que s'ha deixat anar** (M5Unified #91, 19/11): el GT911
     la té baixa mentre hi ha un marc que ningú no ha llegit —i la torna a polsar mentre l'amfitrió no llegeix—, o sigui que una son
     armada amb la línia ja baixa desperta a l'instant i una altra vegada a cada intent; la baixada llegeix l'estat, l'escriu a zero i
     espera que el pin sigui alt abans d'armar, i si no ho és al cap d'un segon el dit no és una porta d'aquella son. Amb el dit com a
     porta el corrent no es talla, o sigui que qui torna la placa a l'hora és el temporitzador del xip. **En una placa sense tàctil el paràmetre s'ignora** (0.190.17, l'amo: *«a koala,
     atom i matri[x] simplement s'ignora el paràmetre»*): el commutador es desa com a qualsevol altra placa i el que la placa contesta és
     `can_wake_on_touch()`, que allà és fals, o sigui que no hi ha res a armar i la fila de la pàgina no hi és (`caps.touch`).

   **I una represa no espera el comptador, i ho mira abans de tocar res** (0.190.18). Una tornada per una alarma i una **tanda** d'una espera
   llarga (el rellotge ha despertat la placa abans d'hora i només cal rearmar) no poden esperar els cinc, deu o quinze minuts de quietud: el
   que havien de fer ja està fet i cada segon despert és un segon de l'espera llençat. La placa baixa **al primer torn**, i només mentre
   **ningú no l'ha tocada des que ha arrencat** —una mà al vidre, una tecla o un byte de consola fan manar el comptador, que és el que evita
   que s'apagui sota el dit—. Es mira amb dues banderes que `note_the_boot` posa a zero i que només mou una mà de debò
   (`power::note_activity`), i no amb el comptador, que una arrencada també mou.

   **I el vidre no es toca** (0.190.19, l'amo: *«Al despertar de DeepSleep, si es una alarma no has de tocar la pantalla per res … el
   procés es realitza i tornem a deepsleep sense molestar ni gastar pantalla»*). Un despert per una alarma encén el xip, fa la feina i
   torna a baixar **sense enviar ni un píxel al panell**: tot el que es pinta durant aquell run —la barra del boot, la pàgina d'una
   finestra, el que sigui— es queda a la memòria i el vidre conserva exactament la imatge que hi havia deixat el son
   (`display::set_the_glass_asleep`, que la portada de `splash` i totes les `flush` miren). La baixada d'aquell run **no baixa el rail del
   panell** (`IPower::sleep(…, keep_the_panel)`: el controlador es queda amb el seu estat i la seva imatge), o sigui que el despert següent
   no ha de reinicialitzar ni repintar res. **I la 0.190.22 ho fa complir**: `It8951::begin` pregunta la línia de BUSY i, si el
   controlador és viu, l'arma sense `power_on()`; el segon de rampa només el paga un vidre fred (docs/50).

   **I la pantalla només s'encén si l'app cridada demana el primer pla** (0.190.19): una `battery` o un `meteo` que recullen una mostra no
   mostren res i la placa torna a dormir; un temporitzador o un avís del `Clock` que fa `raise` posa la seva pàgina al davant, el vidre es
   desperta per a ella i **la placa no torna a baixar**, perquè una app al davant és una persona que ha de veure-hi. I si qui ha despertat
   la placa és una persona —la tecla del dial o el vidre—, el camí és el de sempre.

   **I el paper es neteja sencer ABANS que la pàgina arrenqui** (0.192.9, l'amo: *«al arrencar de DeepSleep has de fer un splash hide (que
   faci neteja completa del paper) abans de continuar i arrencar l'app»*). El dibuix del son no ha passat per cap pàgina, o sigui que el
   que hi ha al vidre és tinta que no és de ningú i la primera pàgina la deixaria als forats i a les dues franges del sistema. El rentat és
   **un `splash hide`** —el `gc16` del vidre sencer— i té dos llocs, i cap dels dos és dins de l'arrencada de l'app: el bloc del vidre de
   `kernel::boot`, quan la tornada és d'una persona (abans de les alarmes, de la shell i de la pàgina que torna), i `deliver_alarms`, quan
   és una alarma qui aixeca una pàgina (`the alarm brings a page: splash hide, the whole paper, before it starts`). Un despert d'alarma
   **sense ningú al davant no pinta ni un píxel**, que és el que diu el paràgraf de dalt.

   **I una hora que la baixada s'empassa no es dorm** (19/11). El guard de la política era de deu segons, i la baixada sola ja en gasta
   més: dos segons de marge per a la targeta i fins a sis esperant la imatge del son —el registre de la placa del 19/11 va mesurar 2,0 s i
   3,6 s—, i encara hi ha l'arrencada. Amb una alarma a deu o quinze segons, el que la placa anomenava son era un nap de pocs segons o,
   amb l'hora ja passada, un temporitzador d'un mil·lisegon que tornava a arrencar la placa per no res; a bateria, el xip ni tan sols
   s'armava (`at > now` falla) i la placa podia baixar sense cap porta. El guard és de **vint segons**, mira **l'hora que `sleep()` armarà
   de debò** (la més propera entre la demanada i la de la cua), i `power::sleep` refusa una hora que ja és passada abans de tancar res
   ni pintar res; les que són massa a prop les refusa la política i les pot demanar una app, que sap el que vol (docs/50, docs/60).

   **I el motiu del despert, dit clar** (0.190.19): `note_the_boot` escriu una línia amb la porta que s'ha obert i què vol dir —
   `back from a deep sleep, woken by the clock chip: an alarm, nobody in front, the glass is not touched` o
   `… woken by the key of the dial: a person, the page comes back`—, i el xip distingeix el dit del botó (`WakeCause::kTouch` és
   `ext1`, la línia del tàctil, i `ext0` és la tecla del dial).

   **I el vidre ha de dir el son, no el que hi hagi pintat algú** (0.190.18). Una tornada per una alarma conserva la imatge del son —el
   vidre ja la té i tornar-la a pintar serien dos parpelleigs per res—, però una alarma que corre en una **finestra** pinta la pàgina de
   l'app... i aquella app ja no hi és quan la placa baixa, o sigui que el que una persona trobaria en prémer el dial és la pàgina d'una
   cosa que s'ha acabat. La finestra diu que ha pintat (`power::note_a_page_on_the_glass`) i la baixada hi torna a posar la imatge
   (docs/30, docs/50).

   **I el camí de la tecla no depèn de com ha arrencat la placa** (0.190.14): la porta estava lligada a `g_for_an_alarm`, que és
   certa per a **tota** la run d'un despert per una alarma, o sigui que quan aquella alarma es gastava i la cua quedava buida el
   codi queia al camí de l'alarma amb la data a zero: deia `-1767225 s to the next alarm`, la son es refusava —una son fonda sense
   hora no existeix— i el comptador tornava a començar, cada cinc minuts, per sempre. **La regla de «un despert per una alarma no
   torna a baixar de seguida» **ja no existeix** (0.190.19): quan el vidre va dir el son, un despert per una alarma no ha
   pintat res, o sigui que quedar-se despert no ensenyava cap pàgina —només la imatge del son i la placa gastant corrent—. El
   que queda d'aquella regla és el primer torn: `straight_back_down` només és cert si ningú no ha tocat la placa. **I una
   alarma que s'ha de veure no és un cas de la política**: si demana el primer pla (`raise`), `for_an_alarm` de l'arrencada ja
   és fals perquè hi ha una app al davant, i mentre hi sigui la política no dorm. Sense cap alarma a la cua, la placa baixa a
   la tecla —amb el comptador esgotat o perquè la represa no ha de quedar-se desperta per res— i el que hi ha al vidre és la
   imatge del son.

   **I quants segons és «quietud» també es tria** (0.190.9, l'amo: *«afegir selector de temps per l'idle de settings->battery (per
   fer el DeepSleep)»*): la fila **Sleep after** de la mateixa pestanya Battery és un desplegable del sistema amb **tres temps** —**5
   minutes** (el de sèrie), **10 minutes** i **15 minutes**—, que és `cfg.idle` des de Lua (en segons) i `cfg idle 5|10|15` al
   prompt (0.190.186, que també accepta els segons de sempre, `300`, `600` i `900`: són el que escriu l'app, no el que escriu una
   persona). El número es desa, o sigui que el boot el torna a llegir, i la política el llegeix d'allà en lloc de la constant. **El
   terra de vint segons no es mou** (era de deu fins a la 0.190.20): és una altra pregunta —si val la pena un cicle d'apagada
   quan l'alarma és a tocar— i barrejar-les faria que triar cinc minuts canviés quan la placa deixa de despertar-se per una
   alarma propera (docs/30, docs/50).
3. **Rearmar-lo a cada despert**: el xip dispara una vegada. A l'arrencada es llegeix el bit `AF` —per
   saber si va ser l'alarma o el botó `PWR`— i es desarma (`RtcClock::adopt`).
4. **Un sol despertador, i el té el xip.** La cua està ordenada i només s'arma la seva entrada més
   propera, com es feia amb el temporitzador del SoC: la diferència és que ara qui la té armada és
   el xip. Quan en canvia la propera, `RtcClock::arm_wake` escriu l'hora al xip, l'arma i **baixa el
   temporitzador del SoC**, perquè dos armats per a la mateixa data són dues veritats i una seria
   falsa. El del SoC només s'arma quan el xip no pot dir res (no contesta, no té hora, o la data
   se li escapa), i llavors el xip es desarma.
   **I quin dels dos l'ha agafada ho diu el registre** (0.189.149): `#1 armed on the clock chip ...` o `#1 armed on the RTC
   timer of the SoC ...`, amb l'hora de la persona i quants segons falten. És la resposta al *«quin métode»* que l'amo va
   demanar de veure, i surt del propi rellotge (`IClock::wake_keeper`) i no d'una deducció de qui crida.
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

## El bloqueig de la pantalla (0.189.150)

L'amo va demanar un bloqueig que fos **una app i no una barra que s'amaga**: *«El Toggle de la pestanya "Privacy" activa que es
llençi l'app escollida com lock al inici del sistema i al despertar d'un sleep. Afegeix també un selector de temps al panell
Privacy "Lock after:" 5, 10, 15 minutes, per defecte 10 minutes. Aquest comptador torna a començar quant l'usuari deixa
d'interactuar amb la placa, no toca botons físics ni pantalla durant aquest temps, llavors salta l'app lock»*. El nucli no dibuixa
ni una línia del que hi surt: obre la pàgina que `cfg lock` nomena i prou (`lib/kernel/src/lock.cpp`).

| Moment | Què passa |
|---|---|
| **Arrencar la placa** | `lock::boot()` obre la pàgina si el commutador és cert i **la casa no s'arrenca darrere seu**: la demana el pany quan s'obre (`device_lock` llegeix `cfg home`), perquè una casa que s'arrenca darrere la cortina no hi és quan la cortina s'aixeca (0.189.159) |
| **Tornar d'un son lleuger** | `power.cpp` avisa amb `lock::note_a_wake()`; el primer torn que ve després obre la pàgina |
| **Els minuts sense màns** | Cada tecla, cada dit i cada byte de consola tornen a començar el comptador; quan s'acaba, la pàgina puja al davant |
| **El commutador apagat** | Cap dels tres: la placa es queda on era |

- **La pàgina del pany és la primera i la casa ve darrere seu** (0.189.159, l'amo: *«A l'arrencada fes: està habilitat el lock?
  existeix lock? → Arrencar lock → En acabar arrencar Home (si existeix)»*): `lock::boot()` arrenca el pany **abans** que el nucli
  miri `cfg home` i respon el nom de la pàgina que ha posat al davant; el nucli només arrenca la casa si la resposta és buida, i
  qui la demana, quan el pany s'obre, és la mateixa app del pany. Una app que torna d'una sessió no es trepitja: el pany hi va a
  sobre amb `raise_app` i la pàgina torna quan s'obre (docs/30, docs/40).
- **El comptador és una durada i no una hora**: surt de `pal::system().millis()`, el monòton, i no de l'`epoch`, perquè posar
  l'hora endavant no ha de bloquejar la placa i posar-la enrere no ha de deixar-la sense bloquejar mai. És el mateix rellotge
  que compten les alarmes.
- **Qui compta com a mà**: el flanc de pujada d'un botó (`button.cpp`), cada fotograma amb un contacte al vidre
  (`touch.cpp`) i cada byte que arriba a la consola (`kernel.cpp`). Un son no compta com a mà: és el que fa saltar el pany.
- **Els minuts són cinc, deu o quinze i res més** (`cfg lockafter`): la llista és la de la pàgina i un número que la fila no sap
  ensenyar seria un ajust que una persona no pot desfer. El de sèrie són deu.
- **La pàgina no es torna a demanar si ja és al davant** (`lock::locked()`): preguntar-la cada torn seria tancar i obrir l'app
  cada torn, i el comptador torna a començar quan la pàgina ha pujat, o el torn següent la tornaria a demanar.
- **Una placa sense vidre no tapa res**: als quatre entorns sense `LUNOK_GLASS` el bloqueig no fa res i no diu res, perquè la
  consola és tot el que tenen (docs/50).
- **I si el commutador és cert sense cap pàgina triada, ho diu una vegada** —`the screen is set to lock and no lock app is
  chosen`— i prou: una línia per torn és un registre que no es pot llegir.
- **I el candau de la targeta del sistema també la obre** (0.189.151, l'amo: *«L'app "Lock" també ha de sortir»*, i després
  *«Posa el lock amb els de poweroff, sleep i reboot: Power off, Sleep, Reboot, Lock»*): és la quarta de les quatre claus de dalt,
  amb les que paren la placa, i obre la mateixa pàgina de `cfg lock` —la clau no mira el commutador, perquè el commutador decideix
  els tres moments automàtics i la clau és la persona demanant-ho ara—; amb `cfg lock none` la clau no es pinta, com la casa i la
  roda quan no tenen app (docs/30, docs/45).

**El que el bloqueig no fa**: no hi ha PIN (el pany del vidre és el de `device_lock`, que és una app), no compta els minuts
durant un son profund —en tornar, el rellotge monòton no ha corregut i el despert ja bloqueja igualment— i no impedeix que una
alarma amb `raise` es posi al davant d'una pantalla bloquejada: qui decideix això és l'app de l'alarma, i una alarma que no
pogués avisar seria pitjor que un vidre tapat (docs/30).

## Què hi ha avui

| Peça | Estat |
|---|---|
| Rellotge amb època | El que compta és sempre el **del xip**: `gettimeofday` per a l'època i `esp_timer_get_time` per al monòton, amb la marca a la memòria del RTC perquè un son profund no la perdi. Mentre ningú no l'ha posada, `source` és `"none"` i l'època val 0 (el `gettimeofday` de l'ESP32 compta des de l'arrencada i fer-lo passar per època seria mentir). **Al Paper hi ha a sobre la BM8563 amb pila de botó** (`RtcClock`): es llegeix a l'arrencada i **després de cada son que torna**, s'hi escriu a cada `set_epoch` i a cada armat del despert, i mentre ella és la que recorda `source` diu `"rtc"` i l'hora sobreviu a treure el cable. L'emulador: `--now` posa l'època, i des de la 0.59.0 l'hora que el xip recorda **sobreviu entre execucions** —`<state>/clock.bin`, amb el temps que la placa ha estat desendollada comptat— al Paper (`"rtc"`) i als ATOM només amb `--persist` (`"soc"`, que és un reinici i no una placa desendollada); sense cap de les dues, val 0 i diu `"none"` |
| `time.*` | `now`, `now_ms`, `utc`, `date`, `tz`, `leap`, `week` i `caps`. Formats, noms de zona i `set` no hi són (docs/30) |
| Calendari | La regla dels traspàs i el dia de la setmana viuen a `lib/kernel/src/calendar.cpp`; **el traspàs i el primer dia de la setmana són ajustos** des de la 0.170.0 (`cfg leap`, `cfg week`, `time.leap`, `time.week`) i `ui.month` gira amb ells (docs/30) |
| Cua única ordenada | `lib/alarms`, 8 entrades de 112 B (896 B). Només la primera s'arma, i `arm_wake` es crida quan canvia la més propera |
| `alarm.at`/`after`/`cancel`/`list`/`caps` | Hi són. Una app només cancel·la les seves, i el payload és cadena o número fins que hi hagi un codificador JSON |
| Entrega | `on_alarm(id, payload, {late_ms, missed})` entre dos torns, amb la coroutine suspesa |
| Finestra | VM pròpia, un torn, la crida i la VM fora. `console.is_foreground()` hi és `false` i **l'entry s'hi executa igualment**: una app que arma alarmes al seu chunk ha de mirar el flag, o el sistema es crida a si mateix per sempre. El que ha de fer la feina és el ganxo, no el bucle, i el ganxo llegeix el que desarà abans d'escriure-hi (0.189.145) |
| `raise` | L'app del davant es tanca amb els seus comiats (`on_pause`, `on_save`, `on_close`) i la de l'alarma queda al davant |
| Repeticions | Una diària perduda tres dies arriba **una vegada**, amb `missed = 2` |
| `persist` | La cua es desa a la NVS amb una **època per línia**, i les vençudes es disparen en arrencar amb el seu `late_ms`. Sense època no s'escriu res |
| Despert | `can_wake()` és cert a les dues bandes, i a l'ATOM el despert és el temporitzador del RTC i el botó. Una alarma no es pot impedir que es perdi: si no hi ha corrent, es reprodueix en arrencar |
| Bloqueig del vidre | **Una app i no una barra**: `cfg lock` (la pàgina), `cfg lockon` (el commutador) i `cfg lockafter` (5, 10 o 15 minuts) obren la pàgina a l'arrencada, al despert d'un son lleuger i quan passen els minuts sense cap mà (0.189.150, docs/60) |

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

### Una sola hora a tot arreu (0.189.130)

L'hora d'estiu es va separar del desfasament a la 0.182.0 —`cfg dst` és un interruptor propi, i el número del mapa es queda com a
número del mapa— i allò va deixar tres maneres d'arribar a l'hora de la persona escrit a mà en tres llocs: la franja de la
barra dividia l'època i ensenyava **UTC**, la pàgina de la zona horària sumava l'estiu pel seu compte i la dels ajustos no el
sumava i es componia l'aritmètica civil. Amb una zona que no fos zero això són **dues hores al mateix vidre** i un rellotge
posat des d'una pàgina que queda una hora desviat de la que diu l'altra.

La regla és una i el nucli la té tota:

| Crida o porta | Què és |
|---|---|
| `cfg::offset_minutes()` (nucli) | l'offset **efectiu**: el del mapa més seixanta minuts si l'estiu és on |
| `time.offset()` (Lua) | el mateix número, en minuts. Per **ensenyar-lo** |
| `time.date([t])` (Lua) | l'hora de la persona: `time.utc(t)` més l'offset efectiu |
| `time.epoch(y, m, d, h, min)` (Lua) | la inversa de `time.date()`: l'època d'una hora del rellotge de paret. Per **escriure'n una**. Un dia que no existeix torna `nil, E_INVAL` (0.189.142): el trenta-u de febrer no és el dos de març |
| `time.tz([minuts])` (Lua) | el número del mapa i prou: és **l'ajust** que edita la pàgina de la zona horària, no l'hora de ningú |
| `time.dst([on])` (Lua) | l'interruptor de l'hora d'estiu, un ajust propi des de la 0.182.0 |

**I una fila que ensenya un número i en desa un altre ha de dir els dos** (0.189.141): la fila **Time zone** del panell
d'ajustos ensenya l'hora del vidre (`time.offset()`) i desa la del mapa (`time.tz()`), que amb l'estiu marcat són a una hora
de distància. La primera versió desava el número que ensenyava, o sigui que acceptar el valor que ja era al vidre movia el
rellotge de la placa una hora més; ara el que es desa és el que s'ensenya menys l'hora de la casella, i la pàgina ho diu al
registre amb els dos números.

Res més no pot compondre l'hora: **cap pàgina, cap app i cap tros del nucli no sumen l'offset a mà**. La franja de la barra en és
una (`bar.cpp`, `read_status`), i per això va ensenyar UTC fins a la 0.189.130; les dues apps que toquen l'hora —`apps/tz` i
`apps/settings`— fan servir `time.offset()` per ensenyar i `time.epoch()` per escriure, i la dels ajustos va deixar anar la seva
aritmètica civil quan van arribar aquestes portes.

**I la pàgina de la zona no toca el que no ha mogut** (0.189.142): el mapa tria hores senceres i la placa pot estar a una mitja
—`cfg tz 90` és una hora i mitja i `time.tz` no discuteix—, o sigui que el comiat compara amb **el que el mapa ensenyava en
obrir-se** i no amb `time.tz()`: una pàgina que es tanca sense moure el mapa no escriu res, i una que el mou desa el que el
mapa diu. La llista del panell d'ajustos és d'hora en hora (l'amo: *«no he vist mai que fos +X.5, sempre hores +1, -3, +3…»*) i
una placa posada a mà a una mitja hora ensenya la fila buida en lloc d'un número que el desplegable no podria tornar a escriure.

**El que es desa i on**: la zona, l'estiu, el traspàs i el primer dia de la setmana són preferències de la placa i van a la
memòria de claus (`kernel::cfg`, la NVS, amb la paraula que va teclejar la persona). **El RTC només rep l'època** —
`clock->set_epoch_ms`, i ni `cfg tz` ni `cfg dst` hi escriuen mai—, perquè un xip amb l'hora local obligaria a reescriure'l cada
cop que es canvia de zona i dues apps podrien deixar el rellotge desfasat.

Les proves són `test_the_timeclock_of_the_paper_project_keeps_the_day` (l'app que llegeix l'hora, a `test_apps`), el cas del
rellotge de `test_calendar`, que ja dona la volta a l'època, `test_the_time_zone_app_picks_a_zone_and_saves_it_on_the_close` i
`test_the_panel_of_settings_writes_through_the_doors_of_the_system`, que és el que escriu una hora des d'una pàgina i la
compara amb l'època del rellotge amb la zona que hi ha en aquell moment.

### Quina porta s'ha obert, dita per la placa (0.191.5)

Un despert d'un son profund és un reset, i el xip només en diu el motiu amb la paraula **`ext1`**: una de les línies de l'RTC que estaven
armades. En una placa amb tres línies alhora —el BOOT, l'alarma del PCF8563 i el tàctil de la T5S3— això és el mateix que no dir res, i la
línia de l'arrencada deia *«woken by a finger on the glass»* tant si havia estat un dit com si havia estat un botó.

Ara **la placa contesta** (`IPower::the_door_that_opened()`, i a la T5S3 qui ho sap és `esp_sleep_get_ext1_wakeup_status()`, que dona el
mapa de pins de l'`ext1`), i el nucli la fa servir en lloc de la traducció del xip. Una placa que no ho sàpiga diu `nullptr` i tot queda com
era. La nota de la darrera dormida diu la tercera cosa —**per què ha tornat**— a la mateixa línia on ja deia quines portes s'havien armat i
quines eren obertes abans de baixar.

### La línia de la dormida, en paraules (0.190.269)

L'amo, amb una línia del registre a la mà —`[I][POWER] deep sleep until 1794347273309`—: *«Estría be posar-ho en llenguatge humà, no
trobes?»*. I sí: un epoch de mil·lisegons és el que necessita una màquina, i era **l'únic** que aquella línia deia.

Ara diu tres coses, i la tercera només quan cal:

```
[I][POWER] deep sleep until 07:18 UTC (in 14 min)
[I][POWER] deep sleep until 04:13 UTC on 25/6 (in 1 d)
```

- **Quan torna** —l'hora de la placa—, **quant falta** i, si creua la mitjanit, **quin dia**. Les paraules del quant són les d'una
  persona: segons per sota del minut, minuts per sota de l'hora, hores per sota del dia i dies a partir d'aquí, amb la unitat de sota
  només mentre val la pena llegir-la (`7 h 58 min` sí, `12 min 4 s` no).
- **L'hora és UTC i ho diu, perquè ho és**: la banda que dorm no sap el desplaçament de la persona —el sap el nucli, a `cfg`— i una
  hora local inventada seria pitjor que una de certa amb el nom al costat. El «quant falta» no enganya mai, perquè és el mateix a
  qualsevol fus.
- **El format és un de sol per a les dues bandes**: viu a `hal/civil.h` (`human_wait` i `human_other_day`, al costat de la conversió
  del calendari que ja hi era) i l'emulador el fa servir a les seves dues línies de dormida, o sigui que un guió i la placa es poden
  comparar línia a línia, que és el que demana docs/85.

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
dies d'un mes han de dir el mateix (l'app de la zona horària mou el primer dia amb `time.week`; el traspàs es mou amb
`cfg leap` i `time.leap`, i des de 0.179.50 **no té casella en aquella pàgina** —el que hi ha és l'horari d'estiu, que és
una hora de més sobre la zona— docs/30):

- **`cfg leap on|off`** (o `time.leap`): amb el traspàs apagat, `days_in_month` diu que el febrer té vint-i-vuit dies
  **cada any**, i la pàgina d'un calendari ensenya vint-i-vuit. **El dia de la setmana d'una data no es mou**: el 29 de
  febrer segueix caient on cau, perquè això és el cel i no una preferència.
- **`cfg week monday|sunday`** (o `time.week`): `weekday_of` continua tornant 1 = dilluns i 7 = diumenge —el compte no
  canvia— i el que gira és **la columna**: `ui.month` posa el primer dia del mes on li toca segons aquest ajust
  (`cells_of`, a `lib/kernel/src/ui.cpp`) i gira els set noms de la setmana amb ell. Dilluns és el valor amb què aquest
  sistema va néixer i diumenge és l'altre; una setmana que comenci en dimecres no s'accepta.
- **Les dues es desen a l'acte i sobreviuen a un reinici**, com el desfasament de l'hora: són claus de l'cfg (`leap`,
  `week`) escrites amb les paraules que una persona llegeix amb `nvs` (`on`/`off`, `monday`/`sunday`).
