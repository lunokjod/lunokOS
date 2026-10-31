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

1. **Xip RTC amb pila** (M5Paper, DS3231 al Grove…): és la font de veritat quan hi és.
2. **RTC del SoC**: compta mentre hi ha corrent, i també durant el son profund. Es perd si es treu
   l'alimentació.
3. **Còpia a NVS**: a cada hora (o en dormir) es desa `última_època` i `monotònic`. En arrencar, si no
   hi ha cap RTC, es fa servir aquesta còpia i s'anota que és **estimada**.
4. **NTP**: quan hi ha xarxa. Corregeix el desfasament i, si el desfasament és gran, salta.
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

1. Es comprova que la memòria lliure arribi al `mem_kb` de l'app (si no, es descarta amb `E_NOMEM`).
2. Es crea la VM, es carrega l'entry, es crida `on_alarm(id, payload, meta)`.
3. Pressupost **més curt** que el d'una app de primer pla: és una finestra, no una sessió.
4. Es destrueix la VM. El primer pla continua exactament on era.

Dins de la finestra: `console.is_foreground()` és `false`, escriure a la consola no fa res (queda al
log), i qualsevol crida que necessiti el port dona `E_NOTFOREGROUND`.

Cost a mesurar: cada finestra paga l'arrencada de l'app (crear la VM i carregar l'entry). Per això
una app que vol ser barata en segon pla ha de fer que `on_alarm` no carregui mitja app i ha de tenir
el seu estat a `store` o a `app.save`.

### Pujar al davant

Si l'alarma porta `raise = true`, o el codi de dins la finestra crida `app.raise()`, llavors en
comptes de tancar-se:

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
| `light` | L'app (`power.sleep`) o el nucli si no hi ha res a fer | Res: la RAM es manté |
| `deep` | Només l'app, amb permís `sleep` | L'estat de les alarmes i l'època estimada, a NVS |

Abans de dormir, el nucli passa per `on_pause()` i `on_save()` de l'app oberta, i després programa el
despertador amb la primera alarma de la cua. Quan desperta, mira el motiu (`last_wake`) i segueix des
de l'arrencada normal: **la VM no sobreviu a un son profund**, es reconstrueix.

A la v1, `sleep_light` i `sleep_deep` són `false` a totes les plaques: és la fita M6.

## Precisió

| Què | Valor |
|---|---|
| Granularitat de la cua | 1 ms a la RAM |
| Precisió de l'entrega | **Pendent de mesurar a la placa**. A l'emulador, l'app `clock` posa una alarma a 2 s i la rep **3 ms** tard (`late 3 ms`, mesurat el 30/10) |
| Alarmes amb el sistema despert però ocupat | Com a màxim, un torn del nucli |

No es promet cap número de precisió fins que no es mesuri a la placa, i quan es mesuri s'ha de dir
amb què s'ha mesurat.

## Què hi ha avui (30/10)

| Peça | Estat |
|---|---|
| Rellotge amb època | **Fet** a les dues bandes. ATOM: `gettimeofday` per a l'època i `esp_timer_get_time` per al monòton. **Mentre ningú no l'ha posada, la font és `"none"` i l'època val 0**: el `gettimeofday` de l'ESP32 compta des de l'arrencada i reportar-lo com a època va fer dir `now 17 from soc` a la placa, que era mentida. Emulador: `--now` posa l'època; sense l'opció, val 0 i diu `"none"`, igual que la placa. |
| `time.now` / `now_ms` / `caps` | **Fet**. La resta de `time` (formats, zones, `set`) no hi és encara. |
| Cua única ordenada | **Fet** a `lib/alarms`, 8 entrades de **112 B** (**896 B** mesurats). Només la primera s'arma; `arm_wake` es crida quan canvia la més propera i no abans. |
| `alarm.at` / `after` / `cancel` / `list` / `caps` | **Fet**. Un app només cancel·la les seves. El payload és cadena o número fins que hi hagi un codificador JSON. |
| Entrega amb l'app al davant | **Feta**: `on_alarm(id, payload, {late_ms, missed})` entre dos torns, amb la coroutine suspesa. |
| Finestra | **Feta**: VM pròpia, un torn, la crida i la VM fora. `boot.window` fa que `console.is_foreground()` sigui `false` dins la finestra, i **l'entry s'hi executa igualment**: una app que arma alarmes al seu chunk ha de mirar `console.is_foreground()` primer o el sistema es crida a si mateix per sempre (passat a l'emulador el 30/10: amb l'app sense el guard, una alarma cada 2 s, indefinidament). No comprova el `mem_kb` de l'app (encara no existeix) i el que escriu va al prompt del davant. |
| `raise` | **Fet**: es crida `on_pause`, `on_save` i `on_close` de l'app del davant (quan els declara) i després l'app de l'alarma queda al davant. |
| Repeticions i perdudes | **Fetes a la cua**: una diària que s'ha perdut tres dies arriba **una vegada** amb `missed = 2` (prova nativa). |
| `persist` i NVS | **Fet**: `revision()` diu quan la cua canvia i el nucli l'escriu a la NVS (espai `alarms`, clau `queue`) com a text amb una **època per línia**, perquè el monòton torna a començar a cada arrencada. Sense època no s'escriu res. A l'arrencada, `alarms_restore()` la torna a la cua i les vençudes es disparen amb el seu `late_ms`. Emulador: un fitxer per clau a `<fs_dir>/nvs/<espai>/<clau>`. ATOM: `Preferences` i l'iterador de l'IDF. |
| Autonomia sense corrent | **Cap garantida, i el despert sí que hi és (31/10)**: `can_wake()` és `true` a les dues bandes, així que `alarm.caps().wake` val `"light"`. A l'ATOM el despert és el **temporitzador del RTC** (que sobreviu al son profund) i el **botó** a `GPIO39`; a l'emulador és el rellotge que salta. El que no fa ningú és impedir que una alarma es perdi: si la placa no té corrent, l'alarma es reprodueix en arrencar amb el seu `late_ms` (docs/60 § alarmes perdudes). |
