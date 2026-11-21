# Regles permanents d'aquest projecte (llegir abans de res)

Aquestes són les coses que l'amo ha hagut de repetir més d'un cop. No es negocien i no cal preguntar-les.

## L'entorn gràfic és SEU, i jo no el toco mai

**Mai no executo res que obri una finestra al seu escriptori.** Ni l'emulador amb `--gui`, ni cap eina amb X11/SDL/Wayland, ni
`pio device monitor` en una terminal seva. Si una prova necessita pantalla —l'emulador amb finestra, una captura, qualsevol cosa
que dibuixi— es fa **amb el meu propi Xvfb**:

```sh
Xvfb :99 -screen 0 1600x1200x24 &
DISPLAY=:99 ./programa ...
```

i es mata en acabar. Sense `DISPLAY` propi, **no es fa la prova**: es diu que no es pot fer així i es demana permís, però no
s'usa la pantalla de l'amo. *(L'amo: «t'he dit molts cops que no usis les meves X per proves, USA LES TEVES AMB Xvfb.»)*

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
