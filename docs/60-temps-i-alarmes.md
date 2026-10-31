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

1. **Xip RTC amb pila** (M5Paper, DS3231 al Grove…): és la font de veritat quan hi és. **Cap placa
   d'aquest build en té**, i per això `time.caps().source` no diu mai `"rtc"`.
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

`time.caps().source` diu quina s'ha fet servir: `"rtc"`, `"soc"` o `"none"`. `time.caps().synced` diu
si alguna vegada s'ha sincronitzat.

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
`alarm.caps().wake` val `"light"`, l'ATOM programa el temporitzador del RTC i l'emulador mou el seu
rellotge. El que el sistema encara **no** fa és dormir-se sol: el despert serveix quan algú ha
demanat `power.sleep`, que és el que M6 hi ha posat (docs/90).

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

Dins de la finestra: `console.is_foreground()` és `false`, escriure a la consola no fa res (queda al
log), i qualsevol crida que necessiti el port dona `E_NOTFOREGROUND`.

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
| `deep` | Només l'app (`power.sleep`) | La nota de sessió (quina app hi ha al davant, amb quin fitxer i a quina hora s'ha de despertar) a la NVS, i les alarmes persistents |

Abans de dormir, el nucli passa per `on_pause()` i `on_save()` de l'app oberta, i després programa el
despertador amb la primera alarma de la cua. Quan desperta, mira el motiu (`last_wake`) i segueix des
de l'arrencada normal: **la VM no sobreviu a un son profund**, es reconstrueix.

**Les dues fondàries hi són a les dues bandes**, i la placa ho diu: `power.caps().sleep_light` i
`power.caps().sleep_deep` són certs, i `power.caps().wake` diu de què es pot despertar (`"timer,
button"` a l'ATOM). El que **no** fa el sistema és dormir-se sol, i és una decisió: un caràcter que
arribi mentre dorm es perd i aquesta placa no té pantalla per dir-ho a la persona (docs/50).

## Precisió

| Què | Valor |
|---|---|
| Granularitat de la cua | 1 ms a la RAM |
| Precisió de l'entrega | **Pendent de mesurar a la placa**. A l'emulador, l'app `clock` posa una alarma a 2 s i la rep **3 ms** tard (`late 3 ms`, mesurat el 30/10) |
| Alarmes amb el sistema despert però ocupat | Com a màxim, un torn del nucli |

No es promet cap número de precisió fins que no es mesuri a la placa, i quan es mesuri s'ha de dir
amb què s'ha mesurat.

## Què hi ha avui

| Peça | Estat |
|---|---|
| Rellotge amb època | A les dues bandes. ATOM: `gettimeofday` per a l'època i `esp_timer_get_time` per al monòton. Mentre ningú no l'ha posada, `source` és `"none"` i l'època val 0 (el `gettimeofday` de l'ESP32 compta des de l'arrencada i fer-lo passar per època seria mentir). L'emulador: `--now` posa l'època i sense l'opció val 0 |
| `time.*` | `now`, `now_ms` i `caps`. Formats, zones i `set` no hi són (docs/30) |
| Cua única ordenada | `lib/alarms`, 8 entrades de 112 B (896 B). Només la primera s'arma, i `arm_wake` es crida quan canvia la més propera |
| `alarm.at`/`after`/`cancel`/`list`/`caps` | Hi són. Una app només cancel·la les seves, i el payload és cadena o número fins que hi hagi un codificador JSON |
| Entrega | `on_alarm(id, payload, {late_ms, missed})` entre dos torns, amb la coroutine suspesa |
| Finestra | VM pròpia, un torn, la crida i la VM fora. `console.is_foreground()` hi és `false` i **l'entry s'hi executa igualment**: una app que arma alarmes al seu chunk ha de mirar el flag, o el sistema es crida a si mateix per sempre |
| `raise` | L'app del davant es tanca amb els seus comiats (`on_pause`, `on_save`, `on_close`) i la de l'alarma queda al davant |
| Repeticions | Una diària perduda tres dies arriba **una vegada**, amb `missed = 2` |
| `persist` | La cua es desa a la NVS amb una **època per línia**, i les vençudes es disparen en arrencar amb el seu `late_ms`. Sense època no s'escriu res |
| Despert | `can_wake()` és cert a les dues bandes, i a l'ATOM el despert és el temporitzador del RTC i el botó. Una alarma no es pot impedir que es perdi: si no hi ha corrent, es reprodueix en arrencar |
