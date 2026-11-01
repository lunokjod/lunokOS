# 95 · Mesures a la placa (full de camp)

Aquest full és el que cal mesurar **al maquinari**, per què serveix cada número i on va quan ja es
té. Neix de `docs/90`, i quan un número es pren aquí es copia al document que li toca i s'esborra
d'aquesta llista.

Regles de la casa, que valen per a tots els números d'aquí:

- **Un número sense «mesurat a la placa» i sense amb què s'ha mesurat no existeix** (docs/80).
- Cada mesura porta **la data** i **la placa** (v1.0 o v1.1, docs/50).
- Si una mesura no surt com diu «hauria de sortir», **això també és resultat**: s'apunta i s'obre
  feina, no s'esborra la fila.
- Per mesurar durades des de dins d'una app, **sempre `sys.time_ms()`** i mai `time.now()`
  (docs/60): el segon salta quan arriba una sincronització.

---

## A · El rellotge i el son (el que més falta)

### A-1 · D'on ve l'hora i què pot despertar la placa (1 minut)

```
run clock
lua
console.print('wake ' .. power.caps().wake .. ' source ' .. time.caps().source)
exit
```

`run clock` comença dient `now … from rtc (wake yes)` —el `wake` de `time.caps()` només diu si la
placa pot despertar d'algun rellotge—, i la línia de `lua` és la que dona les paraules: ha de dir
**`wake timer, clock source rtc`**. Si diu `soc` o `none` en comptes de `rtc`, la BM8563 no s'ha
llegit i **no val la pena mesurar res més d'aquesta secció** fins que això es digui `rtc`.

**Fet el 31/10 al Paper**: `now 1793483079 from rtc (wake yes)` i `wake timer, clock source rtc`.

| Què apuntar | On va |
|---|---|
| `source` = `rtc` / `soc` / `none` i la línia `wake` | `docs/50` (rellotge del Paper), `docs/90` pas 4 |

### A-2 · **La mesura que falta des del primer dia**: el son profund de deu minuts amb bateria

Respon tres preguntes alhora: el corrent **cau** de debò quan la placa s'apaga, la **alarma del
xip la torna a encendre**, i **quant deriva** el quars del xip.

```
power idle on
run alarmset
600
```

I tot seguit **desendolla el cable USB** (si no el desendolles, la placa no cau i el que es mesura
és el temporitzador del xip, no el quars). Passats deu segons sense ningú a la consola, el nucli
s'apaga sol fins a l'alarma; al cap de deu minuts la placa s'ha d'encendre **tota sola**, arrencar i
ensenyar `ALARM wake up … late N ms`.

Després, amb el cable posat:

```
power last
log
```

| Què apuntar | On va |
|---|---|
| `power last` diu **`(clock)`** → el corrent va caure i va ser el quars. Si diu `(timer)`, la placa **no** es va apagar (el cable era posat) | `docs/50` §«Apagar-se de debò» |
| El número `+N ms` (o `−N ms`) de deu minuts, i el `late N ms` de l'alarma | `docs/60` §Precisió i la fila «Precisió de l'entrega» |
| Que la placa s'hagi tornat a encendre sola (sí / no) | `docs/90` deure 2 («mesurar la política d'oci a la placa») |

**Tres tandes**, no una: una de deu minuts, una d'una hora i una de vuit hores si es pot (una
nit). El que importa és si l'error **creix amb el temps** (deriva del quars) o és fix (cost
d'arrencar).

**El 31/10 es va provar amb el cable posat** i va sortir `woke on the timer`: això vol dir que la
placa **no** es va apagar, i que A-2 continua pendent. És el senyal bo, de tota manera: `(timer)` amb
cable i `(clock)` sense són les dues meitats de la mateixa prova.

### A-3 · La granularitat de minut del comptador (5 minuts)

El comptador del xip, quan l'espera passa de 255 s, compta **minuts sencers** i pot disparar fins a
un minut abans (docs/60). Cal saber-ho de debò per decidir si val la pena armar 255 s + la resta.

```
run alarmset
330
```

(330 s són 5,5 minuts: el pla és de 6 minuts de rellotge.) Un cop despert:

```
power last
```

| Què apuntar | On va |
|---|---|
| El número de `late` / del drift: si surt entre `−60 000` i `+0 ms`, la granularitat és d'un minut tal com diu el full del xip | `docs/60`, i decideix si s'arma en dues tandes |

### A-4 · El son que **no** arriba a la seva hora (2 minuts)

Ha de quedar dit, no mesurat com una deriva: un son que es talla no prova res del rellotge.

```
sleep deep 600000
```

i, abans que passin deu minuts, **prémer PWR** (o posar el cable):

| Què apuntar | On va |
|---|---|
| Que surt `the sleep was cut short by …` i que `power last` porta `cut short` | Ja és a `docs/60` (31/10); es torna a comprovar aquí |

### A-5 · La precisió de l'entrega amb el sistema despert (2 minuts)

```
run clock
```

| Què apuntar | On va |
|---|---|
| `alarm 1: ring, late N ms, missed 0` — el número de mil·lisegons | `docs/60` §Precisió |

**Fet el 31/10 al Paper: `late 0 ms`.**

### A-6 · El son de vint segons des d'una app (2 minuts)

```
run rtccheck
```

Dorm vint segons i, en tornar, diu `the wake was N ms off (positive is late)`.

| Què apuntar | On va |
|---|---|
| El número, i si diu `hour from: rtc` | `docs/60` §Precisió, al costat de A-5 |

**Fet el 31/10, amb el cable posat** (o sigui **son del xip**, no del quars): `woke on the timer,
+1191 ms` i `+1236 ms` en dues tandes, amb el cost d'arrencada a dins. El nucli ja el resta
(`hal::wake_error_ms`), així que **s'ha de repetir amb el firmware nou** per tenir el número net. El
que sí que va sortir net: `a wait of 1000 ms took 1002 ms of the clock` (0,2 %, quatre vegades menys
que el 6 % que semblava el son).

### A-7 · **La memòria RTC sobreviu al son** (3 minuts)

**Primer s'hi escriu**, o el que es llegeix són 7 168 zeros i no s'ha provat res:

```
lua
f = fs.open('/rtc', 'w') f:write('hola') f:close()
exit
run hexdump
```

Després `sleep deep 20000` (o l'alarma de A-2) i, en tornar, `run hexdump` una altra vegada: els
mateixos bytes han de ser-hi.

| Què apuntar | On va |
|---|---|
| Sí / no sobreviu (i si el xip la neteja quan cau el corrent) | `docs/50`, `docs/70` (fila `/rtc`) |

### A-8 · `poweroff` de debò (2 minuts, sense cable)
Sense cable: `poweroff` → la placa s'ha d'apagar del tot (la pantalla no hi és, així que es nota
perquè **la consola desapareix**). Prémer PWR → arrenca i `power last` **no** ha de dir cap deriva
nova.

Amb cable: `poweroff` → el corrent no cau, el xip queda en son profund **sense cap font de
despertar** i la placa només torna si es treu el cable. Apuntar si això és el que es vol o si s'hi
ha de posar el botó com a despertar (EXT0 a GPIO38).

| Què apuntar | On va |
|---|---|
| Amb bateria: es queda apagada i el botó la torna (sí / no) | `docs/50` §«`poweroff`: apagar sense tornar» |
| Amb cable: com es torna a encendre, i si es vol canviar | Decisió a `docs/90` |

### A-9 · La FRAM contesta i aguanta el corrent tret (5 minuts)

La **FM24C02** és el xip que no necessita el corrent, i el que ha de dir la placa és que hi és i que
s'hi pot escriure.

```
run hexdump /fram
lua
f = fs.open('/fram', 'w')
f:write('hola fram')
f:close()
exit
run hexdump /fram
```

Després, **treure el cable i la bateria** (o esperar que el xip perdi l'alimentació) i tornar:

```
run hexdump /fram
```

| Què apuntar | On va |
|---|---|
| Que `/fram` surti a `mounts` (vol dir que el xip ha contestat a 0x50) i que els bytes segueixin allà després de treure el corrent | `docs/50` (fila FM24C02), `docs/70` (fila `/fram`) |
| Si `mounts` **no** la posa, el xip no contesta i el codi no s'ha de tocar: primer el maquinari | `docs/90` |

---

## B · La bateria

### B-1 · Els mil·livolts contra el multímetre (10 minuts, dos punts)

Respon si el divisor ×2 de GPIO35 i l'ADC diuen la veritat (docs/50).

1. Amb la bateria **plena** (o carregada fa poc): punta al `BAT` i al `GND`, apuntar mV.
2. `run battery` (diu `3900 mV, 70 %, [###…]` i `charging: the board cannot tell`).
3. Repetir **a mitges** (que baixi una estona).

| Què apuntar | On va |
|---|---|
| mV multímetre / mV que diu el sistema / % de la corba, als dos punts | `docs/50` (fila Bateria), `docs/90` pas 5 |
| Si cal corregir el factor (avui `{35, 2}` a `lib/hal_paper/`) | Una línia del HAL |

**Vist el 31/10, amb el cable posat (o sigui carregant)**: `4246 mV, 100 %`. Els 4 246 mV són per
sobre d'una cel·la plena —és el carregador— i per això **aquesta mesura només val amb el cable tret**
i amb el multímetre al costat.

---

## C · El disc

### C-1 · Què costa una lectura de flash o de targeta (5 minuts)

`docs/85` fa servir **1 ms** de flash i el doble a la targeta: és una aproximació fins que algú la
mesuri.

```
lua
t = sys.time_ms(); for i = 1, 50 do fs.read('/res/hola.txt') end; console.print((sys.time_ms() - t) / 50)
exit
```

(Les línies del prompt són de **128 B**: una línia més llarga s'ha de partir en dues, i per això el
`print` d'aquí no porta cap paraula.) El mateix amb un fitxer de la targeta (`/sd/…`, substituint el
camí), i una escriptura atòmica —una temporal al costat i un reanomenament— amb `fs.write`:

```
lua
t = sys.time_ms(); for i = 1, 20 do fs.write('/data/lua/t.txt', 'x') end; console.print((sys.time_ms() - t) / 20)
exit
```

| Què apuntar | On va |
|---|---|
| ms per lectura de flash, de targeta i per escriptura | `docs/85` (fila «Cost d'una lectura»), `docs/70`, `docs/90` |

### C-2 · La targeta a 20 MHz (5 minuts, experiment)

Avui va a **10 MHz** perquè els dos canvis es van fer junts i no se sap quin dels dos va salvar la
targeta (docs/90 pas 7).

1. Canviar `kSdPins` a `20000000` a `lib/hal_paper/src/hal_paper.cpp`, flashejar.
2. `mounts` i **deu** `ls /sd` seguits, i tornar a arrencar i repetir.

| Què apuntar | On va |
|---|---|
| Si a 20 MHz la targeta es munta sempre (deu de deu, dos arrencades) o falla | `docs/50` (SD), `docs/90` pas 7 |

---

## D · La consola i la ràdio

### D-1 · La latència del port sèrie (2 minuts)

Amb l'app `ping` al disc i la placa al prompt:

```
tools/serial_ping.py --port /dev/ttyUSB0 --rounds 10
```

(`tools/serial_bridge.py` diu quin port és, i pot ser `ttyUSB0` o `ttyACM0`.)

| Què apuntar | On va |
|---|---|
| Mínim, mediana i màxim de les deu anades i tornades, amb el nom del pont (CP2104 o CH9102) | `docs/80` §«La latència del port sí que es pot mesurar» |

### D-2 · La ràdio i l'hora de debò (5 minuts)

```
wifi scan
ntp
run clock
http https://example.com
```

| Què apuntar | On va |
|---|---|
| Si `ntp` posa l'hora (i `run clock` ja no diu `from soc`), el codi HTTP i quants bytes | `docs/90` pas 6 (ja donat per bo; això és la comprovació) |

---

## E · Les tecles i la memòria

### E-1 · Quina tecla és quina (2 minuts)

La llibreria del fabricant es contradiu entre el capçalari i el README, i això decideix si la tecla
de dalt diu `up` o `down` (docs/90 pas 3).

```
run keys
```

L'app dona una funció a cada **posició** i diu on has premut, així que n'hi ha prou de prémer les tres
tecles **una per una** (no calen tres dits alhora: cada premuda surt a la consola) i llegir què diu.

| Què apuntar | On va |
|---|---|
| Per a cada tecla (esquerra, centre/PWR, dreta): què diu `run keys` (`up`, `down` o el mig). Si la de dalt diu `down`, s'intercanvien dues línies de `kKeys` a `lib/hal_paper/` | `docs/50` (taula `kKeys`), `docs/90` pas 3 |

### E-2 · La memòria de debò (2 minuts)

```
mem
run burst
```

| Què apuntar | On va |
|---|---|
| `free heap` i `min free heap` al prompt; i el que diu `burst` (`heap used`, `peak`, `limit`) | `docs/75` §1 i §2 |

**Fet el 31/10 al Paper**: `run burst` → `100 waits in 101 ms`, `heap used 37 883 B, peak 37 883 B`.
El `limit 65 536 B` que sortia llavors era el sostre per defecte, perquè **la mesura és d'abans de la
0.7.0**: des de la 0.7.0 el Paper contesta **4 194 304 B** (la PSRAM mapejada, docs/75) i aquesta
línia és la que s'ha de tornar a llegir.

### E-3 · Què costa arrencar (1 minut)
És el tros que li queda al número de A-6 quan el nucli ja ha restat el que mesura ell: carregador
d'arrencada, banner, flash, targeta i ajustos. Just després d'una arrencada o de tornar d'un son:

```
lua
console.print(sys.time_ms())
exit
```

El número és **ms des que el firmware comença a córrer**, i és el que s'ha de saber per llegir un
despert amb precisió de mil·lisegons (el de A-6 i el de A-2).

| Què apuntar | On va |
|---|---|
| ms fins al prompt | `docs/60` §Precisió |

### E-4 · La PSRAM hi és i una app la pot fer servir (5 minuts)

```
mem
run burst
lua
console.print('extra ' .. sys.heap().extra_total .. ' free ' .. sys.heap().extra_free)
exit
```

I una app que demani un megabyte de debò, que ja és al disc (`data/apps/heap/`):

```
run heap
```

Ha de dir `holding 60000` i, després, que la memòria ha tornat.

| Què apuntar | On va |
|---|---|
| Que `mem` digui `extra memory  4067 of 4095 KB usable, of 8192 KB on the chip (psram)` i que `sys.heap().extra_total` sigui **4194304** | `docs/75` §«La memòria extra», `docs/50` (fila PSRAM) |
| Si `extra_total` és 0, la PSRAM no ha arrencat: mirar `BOARD_HAS_PSRAM` abans de tocar res més | `docs/90` |

**Fet el 31/10 amb la 0.7.1**: `mem` → `free heap 162 728`, `min free heap 156 648` i
**`extra memory 4067 of 4095 KB free (psram)`** (abans deia `of 8192`, que era el xip i no el que es pot
mapejar). Des de la **0.8.1** la línia diu els **tres** números —`4067 of 4095 KB usable, of 8192 KB on
the chip`— perquè el primer que es va veure a la placa va ser una contradicció (docs/75). Queda la
segona meitat: **`run heap`** i veure `holding 60000`.

**Comprovat al banc el 31/10 amb la 0.8.1** (179 de 179 proves i els tres builds en `SUCCESS`):
`test_the_extra_memory_is_there_for_any_app` mesura el xip a **8 388 608 B** i el que la CPU adreça a
**4 194 304 B**, i l'app d'un megabyte hi cap. El que falta és el mateix número **llegit a la placa**.

### E-5 · L'aire (2 minuts)

```
run air
```

Ha de dir `air 21.5 C 48.0 %` cada dos segons (els números són els que doni la sala).

| Què apuntar | On va |
|---|---|
| Que `caps` digui **`env yes`** al Paper i que `run air` doni una temperatura i una humitat que **quadrin amb un termòmetre** de la mateixa sala (±1 °C i ±5 %) | `docs/50` (fila SHT30), `docs/30` (§`env`) |
| Si `caps` diu `env no`, el xip no ha contestat: mirar el log (`i2c: …`) abans de culpar el driver | `docs/50` |

**Si el número no quadra** amb el termòmetre, no es canvia la fórmula: el full de dades del SHT30 és
el que és, i el que s'ha de mirar és si el xip s'escalfa amb la resta de la placa (l'autocalefacció és
un error conegut d'aquest muntatge i es veu deixant-lo quiet deu minuts).

**El que va dir la placa el primer cop (1/11, versió 0.10.0)**, i que és el motiu pel qual aquesta
prova encara no té número:

```
lunokOS 0.10.0  m5stack-paper
sd: /sd mounted, 15193 MB
i2c: read of 44 failed (ESP_ERR_TIMEOUT)

lunok> caps
  env  no
lunok> run air
this board has no ambient sensor
```

El xip **hi era i contestava l'adreça** (l'escriptura de la comanda no va fallar): el que no hi cabia
era l'espera. El driver d'aquest IDF deixa **100 µs** de clock stretching i el SHT30 n'aguanta fins a
**6 ms**, o sigui que el mestre el va comptar com un bus mort, va resetejar el bus i va deixar el
sensor sense anunciar. La 0.10.1 apuja el sostre a **13,1 ms**; el detall i les comptes són a
`docs/50` («El bus ha de deixar que el xip aguanti el rellotge»).

**I amb la 0.10.1, l'1/11, la placa diu**:

```
lunokOS 0.10.1  m5stack-paper
sd: /sd mounted, 15193 MB

lunok> caps
  env  yes          (14 de 18: la fila passa de «no» a «sí»)
lunok> run air
air 21.1 C 63.5 %
air 21.1 C 63.5 %
```

Dues lectures seguides iguals i sense cap línia `i2c:` al log: **el sensor contesta i la mesura arriba
sencera amb els dos CRCs bons**. El que queda d'aquesta prova és **una** cosa, i és la que cap
número de firmware pot dir: que aquests 21,1 °C i aquest 63,5 % **quadrin amb un termòmetre** de la
mateixa sala (±1 °C i ±5 %).

### E-6 · El tàctil (5 minuts)

El que es mesura aquí és **en quin marc compta aquest xip** —que és el que la crida de Lua necessita
saber— i **la velocitat de mostra**.

```
# a l'arrencada, abans del prompt, surt una línia:
touch: GT911 at 0x5d, config 66, 540x960, 2 fingers
```

**El que va dir la placa l'1/11 amb la 0.11.0**: aquesta línia i, abans, un `i2c: read of 14:8140 failed
(ESP_FAIL)` que **era soroll i ja no hi és** (0.13.0): preguntar a una adreça on no hi ha ningú no és una
avaria, i el bus té ara `present(address)` —una lectura d'un byte que no diu res— perquè el probe
pregunti qui hi ha abans de llegir-li el producte. O sigui que aquesta unitat **contesta a 0x5D** (no a
0x14), la seva configuració és la **66**, compta en un marc de **540 × 960** i està configurada per a **dos
dits** i no cinc: `touch.caps().points` dona 2, que és el que el xip reportarà de debò.

**El marc, tancat l'1/11 amb la 0.18.1**: quatre tocs, un a cada cantonada de la pàgina vertical de
fàbrica (270), llegint el primer marc de cada toc que deia la consola:

| Toc | `raw` del xip | Pàgina |
|---|---|---|
| dalt a l'esquerra (el rellotge) | `46,28` | `46,0` |
| dalt a la dreta | `515,49` | `515,21` |
| baix a l'esquerra | `31,926` | `31,898` |
| baix a la dreta | `500,920` | `500,892` |

La `x` creix cap a la dreta i la `y` cap avall, igual que es pinta la pàgina: **ni quart de volta ni
mirall**, i amb la pàgina de fàbrica el gir és **zero** (`kChipPage = 270`). El que va costar trobar-ho no
va ser el xip sinó un error meu de gir encadenat —un toc al rellotge sortia a `539,900`, a baix a la
dreta— que està explicat a `docs/50` i té prova al PC. La línia de consola que ho va dir **s'ha tret**
(0.18.2): una línia per toc no és una consola que es pugui fer servir. Per a una altra placa o un altre
panell, es torna a posar allà mateix.

| Què apuntar | On va |
|---|---|
| **La velocitat de mostra**: arrossegar el dit de pressa d'una banda a l'altra i comptar quants frames dona el xip en un segon. El `M5GFX` dedueix els Hz de `0x8056` (`5 + (byte & 0x0F)`); aquest número és el que es compara amb el que compta el dit | `docs/50` |
| Si la línia no surt o diu `no answer at 0x14 or 0x5D`, mirar abans el log de la I2C (`i2c: …`): el tàctil va al mateix bus que el rellotge i el sensor | `docs/50` |

### E-7 · El panell: el handshake i la primera pàgina (5 minuts)

El controlador contesta per SPI i el que es mesura aquí és **si la imatge arriba sencera i recta**, en
quin sentit van els nivells i quant triga un `gc16`. La línia de l'arrencada és el handshake sencer:

```
panel: 960x540, image at 0x001236e0, words 03c0 021c 36e0 0012 6d35 7374
```

**El que va dir la placa l'1/11 amb la 0.12.0**: exactament la línia de dalt, i el número que la fa
de fiar és `0x001236e0`, perquè és **l'adreça que el fabricant té escrita a mà al seu
driver** (`_tar_memaddr = 0x001236E0`) quan va deixar de refiar-se de `GetDeviceInfo`. Que el
controlador me'n torni la mateixa vol dir que el que he llegit i el que hi ha al vidre són la mateixa
placa. Els quatre mots següents (`03c0 021c` = 960 i 540, i `36e0 0012` = l'adreça) són els que es
comproven a ull.

**I la pàgina surt recta amb la 0.13.0** (dit a la placa l'1/11): el marc, l'escala de 16 nivells i els
cinc quadres arriben sencers després d'enviar **un sol preàmbul de dades per trama** en lloc d'un per
mot. El que encara no està mesurat és el temps del `gc16` i el sentit dels nivells: el número que dirà
`run panel` quan es torni a córrer.

Abans d'això, el handshake va fallar de quatre maneres i cadascuna té el seu nom al log. Serveix per
no tornar-hi:

| Línia | Què vol dir |
|---|---|
| `SYS_RUN did not go out: busy stayed low (busy reads 0)` | el rail del panell (GPIO23) no ha pujat: el fabricant hi espera **1000 ms** i no n'hi ha prou amb 100 |
| `…: bus refused the prefix` | `ESP_ERR_INVALID_ARG` d'`spi_master.c`: un `CS_KEEP_ACTIVE` sense `spi_device_acquire_bus()` |
| `…: the pack-write register did not take` | el registre `0x0004` no ha acceptat l'escriptura |
| `GetDeviceInfo did not come back (…; words …)` | la lectura dels 20 mots no ha tornat; `ffff` és un MISO en alt, `0000` un en baix |

**I l'error que va costar la pàgina torçada**: `LISAR` no és `0x0200`. Les adreces de debò són a
`utility/IT8951_Defines.h` del fabricant, que les escriu com a sumes i no com a números:

| Registre | Suma del fabricant | Adreça | Què passava amb la meva |
|---|---|---|---|
| `LISAR` (mot baix) | `MCSR_BASE_ADDR (0x0200) + 0x0008` | **0x0208** | escrivia a `0x0200`, que és **`MCSR`**, un registre d'estat del convertidor |
| `LISAR` (mot alt) | `+ 0x000A` | **0x020A** | escrivia a `0x0202` |
| `LUTAFSR` | `DISPLAY_REG_BASE (0x1000) + 0x0224` | **0x1224** | preguntava a `0x0000`, així que `display.idle()` no s'acabava mai |

Què apuntar:

| Què apuntar | On va |
|---|---|
| **Si la pàgina surt recta**: `run panel` dibuixa un marc, una escala de 16 nivells i cinc quadres. El primer cop (1/11) va sortir **torçada**, i no era l'adreça de la imatge: era que s'enviava un `0x0000` davant de **cada** mot dins d'una trama amb el CS aguantat, o sigui el doble de bytes per fila. El fabricant ho pot fer perquè aixeca el CS a cada parell de mots i el controlador es resincronitza allà (vegeu `docs/50`, «El CS no es queda baix…») | `docs/50` |
| **El sentit dels nivells**: el fabricant deixa la pantalla blanca escrivint `0x0000FFFF` a la gram, o sigui que **15 és blanc i 0 és negre** en aquest controlador | `docs/50`, `docs/30` (§`display`) |
| **Quin mode es pot fer servir de debò.** Mesurat l'1/11 amb l'app `squares` i la imatge de 4 bits: `gc16` pinta (la pàgina sencera va sortir blanca i recta) i **`du` pinta però no neteja** —un quadrat negre esborrat amb `du` deixa el rastre gris i, al cap d'una estona, la pàgina té zones més fosques i les vores de les vegades anteriors—. **`du4` i `a2` no van pintar res**: el `present` responia `true` i el vidre no canviava. Conseqüència pràctica: pintar amb `du` i **esborrar amb `gc16` sobre la mateixa regió** és el que deixa el blanc net costant només la regió | `docs/50` |
| **El temps d'un `gc16`**: `run panel` diu els mil·lisegons entre `flush` i `idle`. El fabricant documenta **450 ms** per a `GC16` i **290 ms** per a `A2`. **La primera lectura de `LUTAFSR` no serveix**: el motor no es marca ocupat en el mateix instant, i la mesura va dir `0 ms` (1/11) amb la pàgina encara movent tinta; la prova espera 120 ms abans de la primera pregunta | `docs/50`, `docs/75` |
| **El `init`**: el primer `kInit` és el de dos segons i el que neteja la fantasma; no s'ha de fer servir per a res més | `docs/30` |

---

## Què **no** cal mesurar encara

- **La velocitat de mostra del tàctil**: contar frames amb el dit (E-6). El marc ja està mesurat.
- **El desgast de la flash**: només té sentit amb el sistema escrivint de debò cada dia.
- **L'autonomia**: es dedueix del consum i la bateria, i no es promet fins que el consum estigui
  mesurat amb el mesurador USB (docs/75).

## Com s'apunta un número al document que li toca

1. Es copia la fila a la taula del document destí, amb **«mesurat a la placa el <dia>»** i **amb què**.
2. Si el número **contradiu** el que deia el document, es canvia el document i s'apunta la
   contradicció en una línia (què deia, què diu el maquinari, per què).
3. La fila d'aquest full es marca com a feta o s'esborra: aquest full només té coses per fer.
