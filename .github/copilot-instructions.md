# Regles permanents d'aquest projecte (llegir abans de res)

Aquestes són les coses que l'amo ha hagut de repetir més d'un cop. No es negocien i no cal preguntar-les.

## L'entorn gràfic és SEU, i jo no el toco mai: **tota prova del PC va amb el meu Xvfb**

**Mai no executo res que obri una finestra al seu escriptori.** Ni l'emulador amb `--gui`, ni cap eina amb X11/SDL/Wayland, ni
`pio device monitor` en una terminal seva.

**I la regla és més ampla que «no obrir finestres»**: **qualsevol prova que faci al PC o amb l'emulador —amb finestra o sense,
amb captures o sense— es fa amb un Xvfb propi**, encara que el que executi sigui una comanda que no dibuixa res. El que no es fa
**mai** és córrer res amb el `DISPLAY` de l'amo ni amb el `DISPLAY` que hi hagi a l'entorn:

```sh
# sempre: el meu servidor, el meu número de pantalla, i mort en acabar
Xvfb :99 -screen 0 1600x1200x24 >/tmp/xvfb.log 2>&1 &
XPID=$!
sleep 2
DISPLAY=:99 .pio/build/emulator/program --board … > /tmp/run.log 2>&1 &
EPID=$!
# … la prova …
kill $EPID; kill $XPID
```

- **Res s'executa amb el `DISPLAY` heretat.** Si una comanda no ha de tocar cap pantalla, es diu explícitament amb
  `env -u DISPLAY …` o amb `--no-gui`; si n'ha de tocar una, és `DISPLAY=:99` i prou. **En aquesta màquina l'entorn porta
  `DISPLAY=:0`, que és la pantalla de l'amo**: tota comanda que no sigui `env -u DISPLAY` hi ha de portar el `DISPLAY` escrit.
- **Les captures també**: `DISPLAY=:99 import -window root`, `xwd`, `xdotool`… sempre contra el meu servidor.
- **El número de pantalla és meu** i es tria lliure (`:99` si és lliure); si ja hi ha un `Xvfb` meu corrent es pot reutilitzar,
  però **no s'agafa mai el de l'amo**.
- **Es mata en acabar**, amb els PID concrets (`kill <pid>`, mai `pkill` per nom), i es diu que no queda res obert.
- **Sense `DISPLAY` propi no es fa la prova**: es diu que no es pot fer així i es demana permís, però no s'usa la pantalla de
  l'amo. *(L'amo: «t'he dit molts cops que no usis les meves X per proves, USA LES TEVES AMB Xvfb.» i, més tard, «Has d'usar
  sempre Xvfb per fer proves en el pc i l'emulador.»)*

## El port sèrie és seu

No obrir `/dev/ttyACM*` ni `/dev/ttyUSB*` (obrir-lo amb pyserial reinicia la placa). Gravar i llegir el port ho fa l'amo quan
diu que sí.

## Compilar: `lilygo-t5s3-idf` i prou, de moment

El que es compila per defecte és **`pio run -e lilygo-t5s3-idf`** (l'amo: *«De moment amb que compili a t5s3-idf fem eh?»*). No es
compilen els vuit entorns a cada canvi. **L'excepció**, i s'ha de dir: si el canvi toca una cosa que només compilen algunes bandes
—`lib/kernel/src/no_glass.cpp` (les quatre sense vidre), qualsevol `lib/hal_<placa>/`, `lib/board_*/`, qualsevol cosa dins
`#if LUNOK_GLASS`—, aleshores es compila també la banda afectada (o totes) abans de dir que està fet, perquè una porta nova al nucli
sense la seva resposta al *stub* és un enllaç trencat que l'IDF no ensenya.

## Res de versionar, documentar a fora, compilar fora de mida o cometre sense permís

- **No es fa cap `git commit`, `push`, `tag` ni canvi de branca** si no ho demana explícitament.
- Les proves (`pio test`) no es corren si no ho demana: les apps i el nucli ja s'han validat quan toca.
- Un canvi de codi sí que es compila per veure que compila; el que no es fa és gravar-lo a la placa.

## La llengua

Es parla i s'escriu **en català**. El codi i els comentaris tècnics són en anglès, amb les cites de l'amo en català.

## No s'inventa res del maquinari

Els pins, els registres i les seqüències es copien del **fabricant** (fulls de dades, exemples, `docs/pinmap.md` de la placa) o
es mesuren a la placa. Si no hi és, es diu que no hi és.

## Paraules

- «font» i no «cara» (tipografia).

## `DevelNotes.md`: prohibit de mirar-lo

L'amo ho ha dit amb aquestes paraules: *«grava't a foc que DevelNotes.md NO TENS PERMIS NI PER MIRAR EL CONTINGUT»*, després que
l'obrís per recuperar-hi un secret d'un fitxer que jo havia esborrat per accident.

- **No s'obre, no es llegeix, no es busca-hi res**: ni `cat`, ni `view`, ni `grep`, ni un `python` que en tregui cap línia, ni
  tan sols per comprovar si existeix.
- **No se'n cita ni se'n descriu el contingut**, encara que l'amo hi faci referència.
- **Cap urgència no ho aixeca**: si la solució d'alguna cosa sembla que és allà dins, es diu que no s'hi pot mirar i s'espera que
  sigui l'amo qui ho llegeixi i ho doni.
- **I la mateixa regla val per a qualsevol secret seu** —claus, contrasenyes, notes personals—: no es llegeixen ni s'envien enlloc
  sense un permís explícit demanat abans i donat amb paraules.
