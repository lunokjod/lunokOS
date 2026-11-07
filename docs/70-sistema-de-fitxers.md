# 70 · Sistema de fitxers

## Tot neix a `/`

`/` **no és cap partició**. És una taula de muntatges estàtica. No s'hi pot escriure res directament.

## `/littlefs` és obligatori

Tota placa ha de tenir **almenys un sistema de fitxers**, i el del sistema és `/littlefs`: és on viuen
les apps. Sense ell no hi ha res a executar, així que no és una capacitat opcional com la pantalla o
la bateria.

- El nucli el munta **abans de fer cap altra cosa**, just després del banner.
- La partició es diu **`littlefs`** a `partitions.csv`, i el backend la busca **per aquesta etiqueta**:
  si no se li diu el nom, el core busca un nom per defecte que no és el nostre i falla amb «partition
  could not be found». Passar el nom explícitament és el que fa que munti.
- Tot seguit **escaneja `/apps`** quan algú demana la llista: la carpeta virtual posa la flash
  (`littlefs/apps`) i la targeta (`sd/apps`) en un sol directori —la targeta només si hi és— i deixa
  passar només els noms que el presentador accepta, que és el que responen les comandes (`apps`,
  `ls`, `cat` i `run`) (0.88.20).

A la taula de particions de 4 MB, `/littlefs` té **1 408 KB**: comença a `0x290000` i fa `0x160000`
bytes (1 441 792 B), i l'aplicació es queda `0x280000` bytes a `0x10000`. Els 64 KB del final
(`0x3F0000`) són la partició **`coredump`**, que no és nostra: la necessita el nucli de l'ESP32 per
deixar un bolcat quan el firmware peta. Sense ella, cada arrencada comença amb dos errors de
`esp_core_dump_flash` i una pànic no deixa res a mà.
- **El firmware no formata mai el disc.** El disc el fa `tools/make_disk.py` i el posa la placa amb
  `-t uploadfs`; aquí només es munta. El parany és que la via de socors del core
  (`LittleFS.begin(true)`) no serveix: formata un superblock amb `name_max` **255** i tot seguit el
  seu propi muntatge el refusa (`Unsupported name_max (255 > 32)`, `EINVAL`), o sigui que converteix
  «no hi ha disc» en «un disc que no es munta mai» i, a sobre, trepitja el que hi hagués. Una placa
  sense imatge diu que no hi ha disc i espera (4/11, mesurat a l'ATOM; el lector de targetes ja
  feia el mateix des de sempre).
- Si el muntatge falla, el sistema **arrenca igualment**: deixa l'error al log, obre el prompt i diu
  que no hi ha apps. Una placa que no pot dir res és pitjor que una placa sense apps, i el prompt és
  l'única via de diagnòstic que hi ha.
- Una app amb un manifest il·legible es descarta amb una línia al log i **no atura** el registre de
  les altres.

| Muntatge | Backend | Què hi ha |
|---|---|---|
| `/littlefs` | partició de dades de la flash | `apps/`, `res/`, `cfg/`, `data/` |
| `/nvs` | emmagatzematge clau-valor | claus exposades com a fitxers |
| `/rtc` | **un sol fitxer** sobre la memòria RTC del xip (`hal/memory_file.h`) | **7 168 B** (7 KB): la part dels 8 KB del xip que l'aplicació pot tenir, perquè la ROM i l'IDF se'n guarden la resta. És l'única memòria que **sobreviu a un son profund i mor amb el corrent**, o sigui que hi van els pocs bytes que una placa ha de recordar entre dues arrencades sense tocar la flash. L'espai és **la seva mida i tota lliure, sempre** (`free 7168 of 7168 B`), perquè no s'hi assigna res: és memòria crua i el repartiment el fa l'app. `cat /rtc` el llegeix com a text i `hexdump` l'ensenya en hex i en visible, que és el que cal per a un binari. S'hi escriu amb un mànec (`fs.open`) o amb `fs.write` des de la 0.65.1: una escriptura atòmica necessita un reanomenament que una memòria no té, i el que fa és escriure al lloc —els bytes al començament i la resta a zero— |
| `/data` | àlies | escrivible per app: `/data/<id>/` |
| `/fram` | **un sol fitxer** sobre la **FM24C02** de la placa (`esp32/fm24c02.h`, I2C 0x50) | **256 B** (2 Kbit) i **no són una EEPROM**: és una **FRAM**, o sigui que una escriptura és a la memòria quan torna la transacció (no hi ha els 5 ms de cicle d'escriptura de la 24C02) i l'aguanta 10¹² cicles. No necessita el corrent, i per això hi van els pocs bytes que han de sobreviure a treure la bateria. Només existeix a les plaques que tenen el xip: `mounts` no en posa la fila si no hi contesta. L'espai és la seva mida i tota lliure, com al `/rtc`. S'hi escriu amb un mànec (`fs.open`) o amb `fs.write` des de la 0.65.1, que hi va al lloc: els bytes al començament i la resta de la memòria a zero. `hexdump /fram` l'ensenya |
| `/apps` | **directori virtual: els dos discs en un, i l'únic punt d'entrada de les apps** (0.88.19, 0.88.20) | **`littlefs/apps` i `sd/apps` junts**, amb la flash primer i la targeta després. Un nom que la flash té **i** la targeta també es presenta **dues vegades**: `<id>` és la còpia de la flash i **`<id>-SD` la de la targeta**, i cada nom llegeix i corre la seva —`run squares` és l'app de la flash i `run squares-SD` la de la targeta—, en lloc d'una desapareixent darrere l'altra. La carpeta **es crea si no hi és** (a la flash i a la targeta): el banc no porta cap app i `repo install` n'hi posa la primera, i una porta que s'obrís només quan la carpeta ja hi era al boot deixava l'app instal·lada **llistada per `apps` i no oberta per `run`** (`cannot read: /apps/<id>/<entry>`, trobat al banc el 4/11). **És l'únic camí per on el sistema mira una app**: el llistat, el `run` i els fitxers d'una app (`/res/`) hi passen, i mai pel directori real d'una unitat, o sigui que **el sistema decideix què presenta la porta** (`vfs::set_apps_presenter`, instal·lat al boot amb `apps::is_an_app`): una carpeta que no porta `manifest.json` **no es llista, no es corre i no la llegeix ningú** que busqui un obridor; una que en porta i diu una altra placa **sí** que hi és, i el llistat diu què necessita |
| `/res` | àlies, i porta per a una app | `/littlefs/res` per a una persona; per a una app, **`apps/<id>/res/`** del disc on corre (vegeu més avall) |
| `/fonts` | àlies | `/littlefs/res/fonts`: **les cares del disc, una sola còpia per a totes les apps** (3/11, 0.68.0). De només lectura des d'una app, com `/apps`: una cara no és d'una app sinó de la placa. El perquè és un número: les quatre cares de DejaVu són 37 476 B i, quan cada app en portava la seva còpia, el disc les pagava dues vegades (docs/30) |
| `/cfg` | **porta** (no és cap disc) | `<unitat>/cfg/`: la targeta si n'hi ha, la flash si no, o la que la persona hagi triat |
| `/sd` | targeta SD per SPI (FATFS) | **El Paper**, quan hi ha una targeta al slot: el seu `IStorage` la munta i el muntatge només existeix si el muntatge ha anat bé (docs/50, docs/90). Al PC, un directori que es posa amb `--sd <dir>`, i sense l'opció no hi ha `/sd` (docs/85) |

**`ls` de l'arrel és la taula de muntatges, i diu què és cada fila**: `/rtc` i `/fram` surten com a
`file` amb la seva mida, no com a `<dir>`, perquè són una memòria d'un sol fitxer i no un directori
(`MemoryFile::list` no té res a llistar).

**Una targeta i una partició no són el mateix.** FATFS munta **una** partició FAT de la targeta (la que troba), i els números de `fs.space` són d'aquella partició, no de la targeta. Amb una targeta de 16 GB que porta una partició de 2,8 GiB, `fs.space` diu 2,8 GiB i fa bé: el que no es munta no existeix per al sistema (31/10, mesurat a la placa).

**Què hi pot fer una app**: **llegir** sí —llegir és lliure, excepte el racó d'una altra app—, i **escriure no**: escriure només es fa dins de `/data/<id>/` (docs/30). Una targeta és, avui, d'on les apps llegeixen.

Els **àlies no són còpies**: són una entrada més a la taula de muntatges que apunta a un camí dins
d'un altre muntatge. Ocupen una fila i una comparació de prefix, res més. L'únic que no vol dir el
mateix per a tothom és **`/res`**: per a una persona és el `res/` del disc —els fitxers d'exemple—, i
per a una app és **el seu** `apps/<id>/res/` (vegeu-ho més avall).

**Bytes per als sistemes de fitxers, entrades per a l'NVS.** `mounts` dona `free N of M B` on hi ha
bytes a comptar i `entries N of M` a l'NVS, que no en té: el magatzem té un nombre fix d'entrades i
és aquest el número que diu si el pròxim desat hi cabrà. A l'ESP32 surt de `nvs_get_stats` (usades de
total); al PC i a l'emulador no hi ha sostre —una clau és un fitxer— i diu només quantes n'hi ha
(`total` val 0 i no s'imprimeix). `fs.space("/nvs")` continua responent `E_UNSUPPORTED`: no hi ha
bytes que donar.

## `/nvs` com a fitxers
L'emmagatzematge clau-valor té espais de noms i claus. S'exposa com un arbre de dos nivells:

```
/nvs/lunoknet/ssid          → la xarxa que el sistema ha desat
/nvs/wifidrv/pass           → la contrasenya, que a la placa és dins del blob del driver
/nvs/session/app            → l'app que hi havia al davant quan es va dormir
/nvs/alarms/queue           → la cua d'alarmes persistents, una època per línia
```

**A l'emulador aquestes claus són fitxers de `<state>/nvs` i sobreviuen entre execucions**, que és el
que fa la flash de la placa: `wifi` després de tornar a arrencar diu el mateix nom i `wifi forget`
esborra el nom (docs/85).

| Operació de fitxer | Què fa a NVS |
|---|---|
| Llegir | `get` |
| Llistar el directori d'un espai | iterar les claus |
| Escriure | **No hi és encara**: el muntatge és de només lectura (docs/90). El nucli escriu a la NVS per `IKeyValue`, no per aquest muntatge |

Límits que s'hereten i s'han de dir:

- Claus de **15 caràcters** com a màxim.
- Valors de mida finita per entrada (blob d'NVS); un valor més gran dona `E_TOOBIG`.
- **No es pot afegir al final**: obrir en mode `"a"` dona `E_UNSUPPORTED`.
- No hi ha directoris de més de dos nivells.

### Què hi ha avui (30/10)

La crida `IKeyValue` de la HAL **ja està implementada a les dues bandes**, i les dues proven
`sys.caps().nvs`:

| Banda | Com |
|---|---|
| Emulador | Un fitxer per clau: `<state_dir>/nvs/<espai>/<clau>` amb el valor cru a dins, i l'espai és un directori. Es pot mirar amb `ls` i amb un editor. |
| ATOM | `Preferences` per llegir, escriure i esborrar, i l'iterador de l'IDF (`nvs_entry_find`/`nvs_entry_next`) per recórrer un espai; `Preferences` no en té. |

Noms amb `/`, `\` o `.` es refusen: són un espai i una clau de l'NVS, no un camí (i `..` és la manera
clàssica d'escriure fora del lloc). Qui la fa servir avui és la cua d'alarmes: espai `alarms`, clau
`queue`, amb una línia de text per alarma i una època a cada línia (docs/60).

Encara **no** hi ha el muntatge `/nvs` com a fitxers per a les apps: això és el que falta de M4, i el
`IKeyValue` és la peça sobre la qual es farà.

## El muntatge `/nvs` (30/10)

`/nvs` ja és a la taula de muntatges, **de només lectura** fins que arribi l'altra meitat de M4
(escriptura, `mkdir`, `remove` i `rename`). El que ja es pot fer:

```
lunok> ls /nvs
  <dir>   alarms
  <dir>   unit
lunok> ls /nvs/alarms
  file     queue  (768 bytes)
lunok> cat /nvs/alarms/queue
```

Regles que se'n dedueixen i que són fàcils de trencar si no es diuen:

- **Dos nivells i prou**: `<espai>/<clau>`. `/nvs/a/b/c` no és cap fitxer i dona error, perquè la
  clau-valor no té més profunditat.
- **Un fitxer obert alhora.** El valor es llegeix sencer en un buffer fix de **1 024 B** i el
  muntatge en refusa un segon: obrir un fitxer **no pot reservar memòria** (docs/75), i els valors
  de la clau-valor són petits per definició. Un valor de 1 024 B o més es refusa, no es talla.
- **La mida d'un fitxer es llegeix per saber-la**: la clau-valor només la dona llegint el valor. Si
  ja hi ha un fitxer obert, demanar la mida d'un altre es refusa en lloc d'endevinar-se.
- **Només un blob és un valor.** L'NVS de l'ESP32 guarda també enters i cadenes, i l'IDF en té de
  propis (`phy`, `nvs.net80211`, que surten a `ls /nvs`). Una clau d'un altre tipus **es llista amb
  mida 0 i no es pot llegir**: llegir-la demanaria a `Preferences` la llargada d'un blob que no ho és,
  i això deixa un error al log per res. Passava el 30/10 amb `phy/cal_version`.
- El que **no** és nostre no es toca: `/nvs` no esborra ni escriu res de moment, i quan ho faci serà
  només a l'espai que li demanin.

## Resolució de camins

1. El camí és absolut. Un que no comença per `/` no es refusa: se li posa un `/` al davant, perquè des
   del prompt s'ha de poder escriure `ls apps`.
2. Es prohibeix `..` (error `E_INVAL`). Això és també la barrera que impedeix que una app surti del
   seu directori. Un `.` sol no es tracta de cap manera especial.
3. Màxim **128 bytes**. No hi ha cap comptador de nivells: el que limita és la llargada del camí.
4. Es busca, a la taula de muntatges, el **prefix més llarg** que encaixi (taula de fins a **deu**
   entrades, escaneig lineal, comparació de bytes).
5. La resta del camí es passa al backend, que no veu mai el prefix.

Els camins distingeixen majúscules i minúscules, i no es normalitzen accents: `Fitxer.epub` i
`fitxer.epub` són dos fitxers.

## Fitxers oberts

Un **pool estàtic de 8 entrades**. Obrir no reserva memòria: agafa una entrada del pool. Si no n'hi ha
cap de lliure, `E_NO_HANDLES` (i l'app ha de tancar els que no fa servir, no esperar que el
recol·lector ho faci).

Cada entrada té **una sola finestra** de lectura.

## Lectura per finestra

Mai es carrega un fitxer sencer. Un fitxer obert té un tros fix de RAM —la finestra— i sap quin tros
del fitxer té a dins.

```
fitxer:  [................................................]
finestra:                  [####]
                            ^ inici alineat a un múltiple de la mida
```

Regles:

- **Si la lectura cau dins la finestra**, es copia de la RAM. Cap accés al disc.
- **Si no hi cau**, es mou la finestra: `seek` al backend i recàrrega. L'inici s'**alinea cap avall a
  un múltiple de la mida de la finestra**, de manera que una lectura seqüencial fa **una recàrrega per
  finestra** i no una per lectura.
- Els salts curts cap enrere que cauen dins la finestra són gratis; un salt llunyà costa una
  recàrrega.
- La finestra és de **512 B i és fixa**: `fs.open` no la deixa triar (`kFsWindow` a `lib/luabind`), i
  tampoc no hi ha cap `opts` enlloc. Fer-la configurable seria una crida nova i ningú no l'ha demanada;
  si mai cal, el número viu en una constant i el canvi és d'una línia.

### Sostres

| Crida | Sostre |
|---|---|
| `f:read(n)` | `n` bytes; no hi ha cap límit propi més enllà de la memòria de l'app |
| `f:read("*l")` | una línia **sencera**: la finestra que porta el salt de línia torna enrere el que sobra |
| `f:read("*a")` | fins al final del fitxer; el límit és la memòria de l'intèrpret |
| `fs.read(path[, max])` | el fitxer sencer, o com a molt `max` bytes si l'app en posa |
| `http.get(url, max_bytes)` | `max_bytes`; si la pàgina és més llarga, `E_TOOBIG` i no arriba res tallat (docs/30) |
| `http.download(url, path)` | **cap**: la pàgina va sencera al fitxer, i és el disc el que diu si hi cap (docs/30) |

**Els sostres de lectura són de l'app, no del sistema.** Fins al 31/10 hi havia un 4 096 B dur que
responia `E_TOOBIG` sense llegir res (i `*l` tallava les línies a 256 B en silenci): era la finestra
interna filtrada a l'API. Ara un fitxer de qualsevol mida es llegeix, i el que limita és la memòria de
l'intèrpret: un fitxer que no hi cap es llegeix a trossos amb `fs.open`, que és el que fa tota la vida
un lector de llibres.

## Accés aleatori

`fs.read_at(path, offset, len)` obre, llegeix i tanca en una crida, amb la finestra alineada a
`offset`. És el que fa servir un lector de ZIP/EPUB per anar a l'índex central sense obrir res.

## Escriptura

- **Amb memòria intermèdia**: s'escriu quan el buffer s'omple, en tancar o en `flush()`.
- **Atòmica per a fitxers sencers** (`fs.write(path, data)` i els manifests): s'escriu a
  `path.tmp`, es buida, i es reanomena. Un tall de corrent a mig escriure deixa el fitxer vell, no un
  fitxer a mitges.
- Cost: una escriptura atòmica toca la flash dues vegades (el temporal i el reanomenament). A la flash
  això compta per al desgast, i per això el log **no** va mai a fitxer.

## On pot anar una app

Una app viu en sis llocs i enlloc més (docs/30). El que no hi és no és que estigui amagat: li
respon `E_INVAL`, perquè l'app ha de saber que ha demanat una cosa que no és seva.

| Camí | Llegir | Escriure |
|---|---|---|
| `/data/<id>/` | sí | **sí**, i el límit és el disc |
| `/cfg/` | sí, el seu `cfg/<id>/` | **sí**, el seu `cfg/<id>/` |
| `/rtc` | sí | **sí**: és memòria crua i **qui hi escriu decideix el format** |
| `/fram` | sí | **sí**, pel mateix motiu: memòria crua i el format el decideix l'app |
| `/res/` | sí, el seu `apps/<id>/res/` | no: el que una app porta és estàtic |
| `/apps/` | sí | no |
| `/fonts/` | sí, les cares del disc (`res/fonts/`) | no: una cara és de la placa i no d'una app |
| `/sd/` | sí | no |
| `/littlefs/`, `/nvs/` — les unitats del sistema | **no** | no |
| `/data/<altra>/` | no | no |
| **el camí que el prompt li ha nomenat** (0.65.0) | **sí** | **sí** |

**El `/rtc` no és una unitat del sistema com `/littlefs` o `/nvs`**, i per això hi entra una app: és
memòria sense mapa ni claus, i el format el posa l'app que hi escriu (docs/50). Es llegeix amb
`cat /rtc` des del prompt i amb `fs.read`/`fs.open` des d'una app, i per escriure-hi hi ha el mànec
(`fs.open` + `write`) o `fs.write` **des de la 0.65.1**.

**`fs.write` sobre una memòria crua va al lloc i no a un temporal** (0.65.1): `/rtc` i `/fram` són *un*
fitxer cadascun i no hi ha `/fram/x.tmp` on escriure per després reanomenar, o sigui que el camí del mig
donava `E_NOENT` —el `no such file or directory` que veia `run hexedit /fram` en desar (4/11). El que fa ara
és escriure els bytes al començament i **posar la resta de la memòria a zero**, perquè el que s'ha escrit és
el que s'ha de poder tornar a llegir; i una escriptura que no hi cap sencera és `E_NOSPC` i deixa la memòria
com era, que mitja memòria canviada no es pot desfer. Per mirar-la, `cat /rtc` si és text i `hexdump` si és
binari: ensenya l'offset, setze bytes en hex i els mateixos setze entre barres, amb un punt allà on el
caràcter no es pot imprimir.

**No hi ha quota.** El que doni el disc. La quota de 64 KB que hi havia fins al 31/10 va fora: el Paper
té 11,9 MB de disc i una app que desa un llibre no hi cabia, i una quota és un número arbitrari
mentre que l'espai lliure no ho és. Quan el disc diu que no, l'escriptura respon `E_NOSPC` i el fitxer
vell queda sencer.

**I els flags dels muntatges no són la garantia d'això.** `mounts` ensenya `ro` a `/apps` i `/res`
perquè és el que veu **una persona**; una app no arriba mai a aquests camins amb el flag, hi
arriba amb `fs.*` i la taula de dalt. `/littlefs` és `rw` perquè és el disc de la persona, i `/cfg`
també, perquè és la porta al que l'app ha de poder desar.

## On van els recursos d'una app

`/res` és de l'app, i no del disc: `fs.read('/res/salutacio.txt')` llegeix
**`apps/<id>/res/salutacio.txt`**, al costat de l'app i al disc d'on surt. El `res/` del disc
(`/littlefs/res/...`) és el que una persona hi ha deixat —fitxers d'exemple, la prova del camí
llarg— i una app **no el pot llegir**: és el disc del sistema i li respon `E_INVAL`, com qualsevol
altre camí que no és seu.

Tres coses que això compra:

- **Els recursos viatgen amb l'app**: `res/` viu dins del directori de l'app (`apps/<id>/res/`) i se
  n'emporta amb ella. Un `res/` compartit al disc no pot anar amb ningú, i l'app el llegeix **del
  disc del qual surt** per la porta: `/res/x` és `/apps/<id>/res/x`, i el nom del camí ja diu el disc
  —`/apps/reader/res/x` és la flash i `/apps/reader-SD/res/x` la targeta— (0.86.0, 0.88.20, docs/40).
- **Dos apps poden tenir el mateix nom de fitxer** sense trepitjar-se, perquè cadascuna té el seu.
- **El camí que escriu l'app no porta el seu id dins**, que és el que fa que una app es pugui copiar
  d'un disc a un altre sense canviar-ne el codi.

I el que **no** pot: **escriure-hi**. El que una app porta és el que és; el que canvia es desa a
`/cfg/` o a `/data/<id>/`, i `fs.write('/res/x', …)` respon `E_INVAL`. **Una baixada és una escriptura i
segueix aquesta mateixa taula**: `http.download(url, '/data/<id>/x')` hi escriu i `http.download(url,
'/sd/x')` respon `E_INVAL`, perquè una targeta és de la persona i no de l'app (docs/30). El disc d'on llegeix l'app és
de només llegir per a ella (`/apps` és `ro`), i el rewrite no ho canvia: només hi afegeix l'id.

**El camí que la persona nomena** (0.65.0). `run edit /littlefs/notes.txt` és una app i un fitxer, i aquell
fitxer és de la persona: l'app el pot **llegir i escriure mentres viu**, i res més del disc de la persona. No
és una porta oberta —`fs.write('/littlefs/altre.txt')` des de la mateixa app segueix dient `E_INVAL`— sinó
l'autoritat del prompt sobre el que la persona ha escrit a la comanda. És el que fa que un editor desi on li
han dit, i el que fa que una app que obre un fitxer d'un altre lloc no el pugui canviar sense que el prompt
l'hi hagi donat (docs/30). La paraula del prompt viu en una pila que desapareix quan la comanda acaba, o
sigui que el nucli se'n guarda una **còpia** (`luabind::app_path`).

**Les imatges d'una app viuen aquí i es llegeixen amb `ui.image`** (0.63.0, docs/30): el camí és el de
l'app (`/res/plan.png`), el sistema l'obre amb la mateixa política que `fs.read` i el descodifica fila a fila
dins del canvas. L'exemple `run images` porta **una imatge de cada família que el sistema llegeix** a
`apps/images/res`, i cada una ensenya el que aquell format sap fer: `photo.jpg` (una fotografia),
`poster.bmp` (tinta plana en 24 bits), `logo8.bmp` (vuit tintes en una paleta de 8), `cartoon.gif` (una
paleta i una cantonada transparent), `icon.ico` (una icona amb alfa), `lines.png` (línies primes, que no les
sua), `shades.pgm` i `colours.ppm` (dos degradats) i `checks.pbm` (un bit). El **mateix pla en sis formats**
—el que demostra que els sis descodificadors donen els mateixos píxels— viu amb les proves, a
`test/test_img/data/plan.*`, perquè una prova vol que siguin iguals i una pàgina vol que siguin diferents. Una
imatge és un fitxer de fins a **2 MB** i el que el descodificador necessita a més a més —el que s'infla d'un
PNG, la banda d'un JPEG— va i torna amb el dibuix: no queda res al heap (docs/75).

I això té una conseqüència sobre `open`: **un fitxer que es passa a una app ha de ser un que l'app
pugui llegir** —la targeta (`/sd/…`) o un lloc seu—. Els fitxers d'exemple del disc
(`/littlefs/res/…`, els de `data/res/`) són per a `cat`; `open res/hola.txt` troba l'app per
l'extensió i després l'app no el pot llegir, perquè el `/res` que veu és el seu.

## On es desa la cfg d'una app

`/cfg` és un **directori virtual**: no és cap disc, és una porta. Darrere hi ha `cfg/<id>/` en una
unitat, i quina unitat és ho tria el sistema, no l'app:

| | |
|---|---|
| Què veu l'app | `/cfg/` → és el seu `cfg/<id>/`, i prou |
| On és de debò, per defecte | la **targeta** si n'hi ha al slot; si no, la flash |
| Com ho sap la persona | `cfg` a la consola, o `fs.cfg()` des de Lua: `{unit="sd"\|"littlefs", dir="/sd/cfg/<id>"}` |
| Com se'n canvia | `cfg sd` / `cfg littlefs` / `cfg auto`, o `fs.cfg("sd")` |
| On es desa la tria | a l'NVS, `cfg/unit`, així que sobreviu una arrencada |
| Què més hi va | **les apps que s'instal·len** (0.86.0): `repo install <app>` les baixa a `apps/` de la unitat triada, i `cfg` ho diu (`apps install into: /sd/apps`) |

**El teclat és una altra configuració del sistema i viu a la mateixa porta** (0.93.0): `cfg keyboard` diu quin
hi ha i quins n'hi ha, i `cfg keyboard qwerty` o `cfg keyboard azerty` en tria un. La tria va a l'NVS
(`cfg/keyboard`) i l'arrencada la llegeix; un nom que aquest build no té es refusa amb la llista i no canvia
el que hi havia. No és la cfg d'una app: és del sistema sencer, com la unitat, i per això el `cfg` de la
consola la mostra amb la resta d'ajustos (docs/30, docs/90).

**I el dibuix del logotip de l'arrencada és el tercer ajust del sistema** (0.115.0): `cfg boot` en dona la
llista amb la imatge que el disc té per a cada mot —`boot` i `boot-alt`— i marca el que surt ara, i
`cfg boot boot` o `cfg boot boot-alt` en tria un. Va a l'NVS (`cfg/boot`) i l'arrencada el llegeix **abans de
pintar res**, que és el motiu de ser d'aquest ajust: el logotip és el primer que es veu i és l'únic que es pot
triar mirant-lo. És una **paraula** i no un camí —el que es desa és un mot del món de la splash, i un camí
deixaria que un ajust fes obrir qualsevol fitxer al primer alè de la placa—, i un mot que aquest build no
té es refusa allà on s'escriu, amb la llista, sense tocar el que hi havia (docs/30, docs/50).

**Els dos fitxers temporals d'una instal·lació** (0.88.9): mentre baixa, `repo install` deixa dins del
directori de treball el llistat de la carpeta que està llegint (`.listing.json`) i el pla de les carpetes
que falten (`.folders`, una línia per carpeta amb el camí que té dins de l'app). Tots dos desapareixen
quan l'install acaba, i també quan falla a la meitat.

**I l'app no té nom fins que és sencera** (0.88.14): tot el que baixa va a `apps/.<id>.install` i només
al final el directori sencer es renombra a `apps/<id>`; si la cadena mor al cinquè fitxer de nou, el
directori de treball s'esborra sencer i l'app que hi havia —si n'hi havia una— queda intacta, perquè
abans se la mou a `apps/<id>.old` i es torna a posar si l'últim moviment falla. És el que fa que `apps`
no compti mai una meitat d'app: el nom és el que diu que l'app hi és i no un fitxer de dins.

Tres coses que això compra i que abans no hi eren:

- **La targeta és el disc d'una persona**: es pot treure, posar en un altre aparell i tornar. Una app
  que desa la seva configuració a la targeta se l'emporta. Per això la targeta va primer.
- **Fallback dit en veu alta**: `cfg` amb la preferència `sd` i sense targeta deixa un `cfg: no card;
  the settings go to the flash` al log i va a la flash. El que no fa mai és desar-ho en un lloc i
  dir-ne un altre.
- **Una unitat que no hi és es refusa**: `fs.cfg("sd")` sense targeta torna `nil` i `E_UNAVAIL`;
  `cfg sd` sense targeta diu `cfg: there is no card`. Una preferència acceptada i ignorada deixaria
  la persona confiant una targeta que no hi és.

**La porta no és una escletxa.** `/cfg` es reapunta a una fila de la taula de muntatges, i el camí
es reescriu a `/cfg/<id>/` **abans** de resoldre'l: una app no pot arribar a `/cfg/<altra>/` perquè
no hi ha cap nom amb què demanar-ho, i un `..` es refusa igual que a la resta del sistema (prova
`test_the_settings_of_an_app_go_behind_the_door_of_cfg`). **La consola sí que veu el `/cfg` sencer**:
qui escriu a la shell no és cap app i no té id, així que `ls cfg` és el `cfg/` de tothom, i és com
s'ha de poder mirar què ha desat cadascú.

El que **no** fa encara: repartir-se entre dues unitats (tota la cfg d'una app viu en una de sola) ni
canviar de unitat amb els fitxers a dins — canviar de unitat no mou els fitxers que ja hi són, i això
és el que fa que `cfg auto` amb una targeta nova comenci amb `cfg/<id>/` buit.

## Veure els muntatges i l'espai (30/10)

La comanda `mounts` de la consola diu, per a cada muntatge, el tipus, si és escrivible i **quant espai
li queda**. Les files d'avui, en l'ordre en què surten:

| Fila | Escrivible | Què diu de l'espai |
|---|---|---|
| `/littlefs` | **sí** | bytes: el disc de la persona |
| `/apps` | no | els mateixos bytes, és el mateix disc |
| `/res` | no | els mateixos bytes |
| `/data` | **sí** | els mateixos bytes |
| `/sd` | **sí** | bytes de la targeta: al Paper amb targeta al slot, al banc amb `--sd` i un model amb ranura |
| `/rtc` | **sí** | bytes: **un sol fitxer** de 7 168 B, la memòria del RTC del xip (docs/50). No té entrades |
| `/fram` | **sí** | bytes: **un sol fitxer** de 256 B, la FM24C02 del Paper; només hi és si el xip contesta (docs/50) |
| `/nvs` | no | **entrades** (`entries N of M`), perquè el magatzem no té bytes |
| `/cfg` | **sí** | els bytes de la unitat on és oberta la porta, i el tipus diu quina és |

Dos dels números que demana M4 surten d'aquí (espai lliure després d'`uploadfs`), i el tercer
(temps d'una escriptura atòmica) el diu l'app `scratch`, que compta els mil·lisegons del seu
`fs.write` amb `sys.time_ms()`.

## El que diu el backend quan el fitxer no hi és (30/10)

Mesurat a la placa: `cat` d'un fitxer que no existeix feia sortir una línia de l'IDF abans del
missatge del nucli:

```
[  5349][E][vfs_api.cpp:105] open(): /littlefs/apps/mtab/app.lua does not exist, no permits for creation
cannot read: /apps/mtab/app.lua
```

**És el nucli d'Arduino, no el nostre**: `VFSImpl::open` del core (arduino-esp32 2.0.x) escriu aquest
error per **qualsevol** fitxer que no hi sigui, encara que s'obri en mode `"r"` — el codi mira si el
fitxer és a disc, i si no hi és, cau a la línia del log sense mirar el mode. La resposta no és
silenciar el log: és **no obrir el que no hi és**.

- `vfs::read_file` pregunta abans (`stat`) i no crida el backend. **La pregunta no és un error.**
- `fs.open` d'un camí que no hi és respon `E_NOENT` **sense obrir**, i per això `E_NOENT` i
  `E_NO_HANDLES` no es poden confondre mai.
- `cat` mira primer i diu què passa: `no such file: …`, `that is not a file: …` o `cannot read: …`,
  i **torna fals** quan no ha pogut llegir, que és el que fa que una ordre fallida surti amb codi 1.
- **`ls` fa la mateixa pregunta** (0.86.1): `no such directory: <camí>` quan no hi és, i **la pista de
  on sí que hi ha alguna cosa** —`/sd has: apps` quan s'ha escrit `/sd/app/…`—, que és el que fa trobar
  la lletra que falta. `ls` d'un fitxer el llista amb el nom i la mida, i un error de lectura de debò
  —no un camí que no hi és— és el que encara diu `cannot list: …`. La pista és el que hi ha i no un
  endeví: vuit noms i prou, i la resta comptada.

L'única cosa d'aquest canvi que no es pot veure en una prova nativa és el **soroll**, perquè el log és
de la placa: es comprova amb la traça del port sèrie. **Comprovat a la placa el 30/10**: `cat` d'un
fitxer que hi és (l'entry de `mtab`) i d'un que no hi és (`no-hi-es.txt` → `no such file: no-hi-es.txt`)
surten **sense cap línia `[E]` de l'IDF**.

## `fs.mounts` i `fs.space` (30/10)

L'app pot veure què hi ha muntat, però **l'espai és una pregunta a part**:

- `fs.mounts()` torna `{path, type, writable}` de cada muntatge, sense tocar cap backend. `/apps`,
  `/res` i `/data` són la **mateixa partició** que `/littlefs` i ho diuen així: tres noms, un disc.
  `/cfg` és l'única fila que no és un disc sinó una porta, i el `type` diu on és oberta.
- `fs.space(path)` pregunta l'espai i **camina pel sistema de fitxers**: és la que costa (la mesura
  que va fer baixar l'escriptura de 247 a 83 ms va ser treure-la del camí feliç). No informa de res
  quan el backend no ho sap: `nil` i `E_UNSUPPORTED`, que no és el mateix que «no queda espai».
- **Per veure-ho de mà**: `run mtab` (`apps/mtab/`) llista els muntatges i demana l'espai de
  cadascun, així que ensenya el cost: quatre noms de la mateixa partició són quatre passejades.
  L'arrenca una prova, com `run handles`.
- A l'**emulador** el disc tampoc no és del PC: la flash val el que diu la taula de particions de la
  placa (0x0E0000 = 917 504 B a l'Atom, 0xBE0000 = 12 451 840 B al Paper) i l'ocupat és el que sumen
  els fitxers, amb el que quedi al PC com a sostre; la targeta val 16 GB i el lliure és el del PC, i
  el que falta fins als 16 GB surt com a ocupat. Són les mateixes xifres que a la placa (31/10,
  mesurat a l'emulador i a `docs/85`).
- **I el que costa llegir-hi és el del dispositiu i no un número per a tot** (1/11): al banc, la flash
  paga blocs de 4 KB i els 45 ms d'esborrar el sector d'una escriptura, la targeta sectors de 512 B i la
  FRAM **90 µs per byte**, amb la taula a `emu/disk.h` (docs/85). La NVS és una partició de la flash i
  paga el mateix, i la RTC RAM no paga res perquè és RAM.
- `fs.mounts` i `fs.space` no són una sola crida perquè una app que només vol la llista no ha de
  pagar la passejada.

## Els camins amb espais van entre cometes (30/10)

`cat`, `ls` i `open` parteixen l'argument amb **un sol tokenitzador** (`split_words` a `shell.cpp`), i
dins de cometes dobles els espais no parteixen res:

```
lunok> cat "res/el meu fitxer.txt"
lunok> open "res/el meu fitxer.txt"
lunok> open txt "res/el meu fitxer.txt"
```

- **No hi ha escapes**: una cometa és una cometa. Un camí amb cometes al nom no es pot escriure, i es
  diu així en comptes d'inventar-se una sintaxi.
- **Una línia mal escrita es refusa amb paraules, no a mitges**: una cometa oberta, o més paraules de
  les que l'ordre accepta, dona `bad line: …` i `a path with spaces goes in quotes`, i **no es
  pregunta res** (la traça d'un `open` sense cometes és el testimoni: cap `which one?`).
- **`run` no en necessita**: un identificador d'app no porta espais ni cometes.
- **Canvi de comportament, dit**: abans `open <app> <camí>` agafava **la resta de la línia** com a
  camí, així que un camí amb espais hi entrava sense cometes i sense que ningú ho hagués decidit; i
  `open <camí amb espais>` partia el camí en app i camí. Ara les tres ordres parteixen igual i el
  camí amb espais es posa entre cometes, també quan l'app es nomena. Una regla en comptes de dues.
- **El camí llarg i el camí amb espais són el mateix problema** i tenen la mateixa resposta: el que
  arriba sencer a l'app és el text entre cometes, no una còpia en un buffer petit.

## Vuit fitxers oberts i prou (30/10)

`fs.open` dona un objecte amb un **mànec**; el nucli en porta vuit, que és el mateix número que el
pool de la HAL. El novè dona **`E_NO_HANDLES`**.

- **El pool és compartit**: `fs.read` i `fs.write` també obren i tanquen un fitxer per dins. Amb vuit
  fitxers oberts per l'app, una lectura sencera no hi cap fins que se'n tanqui un. És una conseqüència
  del disseny (un sol pool, cap reserva amagada) i cal dir-la.
- **La mida es pregunta en obrir** i es porta a part: el backend no diu la mida d'un fitxer obert, i
  les escriptures l'actualitzen. `f:seek("end")` usa aquest número.
- **Per veure-ho de mà**: `run handles` (`apps/handles/`) escriu pel mànec, llegeix de tres
  maneres, torna enrere amb `seek` i ensenya el novè refusat. L'arrenca una prova, així que l'exemple
  no pot quedar-se enrere sense que caigui.
- **Un error d'obertura no menteix**: si el fitxer hi és i no s'obre, és `E_NO_HANDLES`, no
  `E_NOENT`. Es va veure amb una trencada feta amb nou llocs al pool del nucli: el novè arribava al
  pool de la HAL i l'error deia «no such file».

## Llegir un fitxer: la finestra (31/10)

L'API de Lua (`docs/30`) llegeix així:

- `fs.read`, `fs.read_at` i `file:read` demanen el contingut **a trossos de 512 B** i el van posant en
  una cadena de Lua. El fitxer mai no és sencer en un buffer de C, i la finestra viu a la pila: **no
  afegeix RAM estàtica**.
- **No hi ha sostre de mida.** Un fitxer de 15 000 B es llegeix d'una tirada, i la cadena creix al
  heap de l'intèrpret; el límit és la memòria que l'app té (docs/75). El que no hi cap es llegeix a
  trossos amb `fs.open`.
- Mesurat amb un sistema de fitxers fals: 5 000 B es llegeixen en **10 crides** i la més gran demana
  **512 B** (`test_fs_read_uses_a_window_of_512_bytes`). Trencar-ho és canviar la finestra per 4 096:
  la prova cau amb «Expected 10 Was 2».
- `file:read("*l")` fa servir la mateixa finestra i, quan el tros que porta el salt de línia porta
  també el començament de la següent, **torna enrere** el que sobra amb `file_seek`. Abans llegia
  byte a byte (una crida al disc per byte) i tallava la línia a 256 B sense dir-ho.

Qui connecta les dues bandes: el nucli coneix el VFS i l'intèrpret no, així que a l'arrencada li
passa quatre operacions (`read_at`, `size`, `is_dir`, `list`). Sense elles, `fs.*` respon
`E_UNSUPPORTED`; és el que permet provar el mòdul amb un sistema de fitxers fals i comptar finestres.

## Escriure: atòmic, amb terra i sense quota (31/10)

- **Atòmic**: `fs.write` escriu `<camí>.tmp` al costat, el tanca i el **reanomena** al lloc bo. Si
  l'escriptura mor a mitges, el temporal s'esborra i el fitxer vell no s'ha tocat. Els dos backends ja
  sabien `rename` i `space` des de M0, així que no hi ha res nou sota.
- **Terra d'app**: una app escriu només dins `/data/<id>/`, i res més (la taula de dalt). Un camí a
  `/data/<altra>` dona **`E_INVAL`** (no `E_NOENT`: no es tracta de preguntar què hi ha dins).
- **Sense quota: el límit és el disc.** Fins al 31/10 hi havia 64 KB per app comptant els seus
  fitxers, i es va treure perquè el número era arbitrari i deixava fora precisament el cas que vol
  aquesta placa (una app que desa un llibre). `E_NOSPC` surt quan el disc diu que no, amb el fitxer
  vell sencer.

**Cost d'una escriptura atòmica, mesurat a l'ATOM el 30/10** amb l'app `scratch` (20 bytes, en
règim): **83 ms**, i el desglossament que donava el sistema en aquell moment era:

| Part | Cost | Què és |
|---|---|---|
| quota | **15 ms** | Llistar el terreny de l'app per sumar-ne l'ús — **aquest pas ja no hi és** |
| prep | **5 ms** | El `stat` del destí i el nom del temporal |
| disk | **62 ms** | Crear el fitxer, escriure, tancar i reanomenar: és la LittleFS i no és nostre |

Amb la quota fora, aquesta mateixa escriptura hauria de baixar uns **15 ms**, i **pendent de tornar a
mesurar-la** a la placa: la línia del log ara és `fs: write N B in M ms` i només dona el total.

**La primera versió en feia 247**, i era culpa nostra: es consultava l'espai lliure del dispositiu a
cada escriptura, i a l'ESP32 això vol dir recórrer el sistema de fitxers sencer (**176 ms**). Ara
l'espai només es pregunta **quan una escriptura falla**, que és quan cal distingir `E_NOSPC` d'un
altre error. Una escriptura que hi cap no necessita saber-ho.
- **El camí es crea sol**: escriure dins el terreny crea els directoris que falten, i `fs.mkdir`
  d'un directori que ja hi és no és cap error.

Conseqüència que cal dir: com que el sostre de la VM és de **64 KB**, una sola cadena de Lua no pot
ser més gran que això. Un fitxer gran s'escriu **a trossos** amb `fs.open` i `file:write`, que va
escrivint el que li donen i no necessita el fitxer sencer enlloc (docs/30).

**Preguntar si un camí hi és no ha de fer soroll.** A l'ATOM, `LittleFS.exists()` obre el fitxer per
llegir, i l'IDF deixa anar `[E][vfs_api.cpp] open(): … does not exist, no permits for creation` per
un camí que simplement encara no hi és — que és la pregunta normal del sistema. El `stat` del port
de l'ATOM fa servir el `stat()` de POSIX, que va pel mateix VFS i no obre res. Es va veure amb
`run scratch` el 30/10: dues línies d'error per escriure el primer fitxer d'una app.

**I els dos mons no parlen el mateix camí.** `LittleFS.open()` rep camins **relatius al punt de
muntatge** (`/apps/burst/burst.lua`), mentre que les crides POSIX van per la VFS del sistema i volen
el camí **sencer** (`/littlefs/apps/burst/burst.lua`). El 30/10 el `stat` nou es va deixar el prefix i
la placa va dir `apps: 0 found` amb tots els directoris llistant-se: els fitxers hi eren i el `stat`
els buscava al lloc equivocat. El port té `kMountPoint` i l'hi posa davant; qualsevol crida POSIX nova
que hi aparegui l'ha de fer servir.

## El deflate, llegit de tornada (1/11, 0.38.0)

Un **zip és el format en què viatja un epub**, i el que porta a dins està comprimit amb **deflate** (RFC 1951).
`lib/inflate` és el descompressor, i no té res més: en una placa que llegeix, comprimir no serveix de res.

| Crida | Què fa |
|---|---|
| `inflate::raw(in, in_len, out, cap, written)` | Deflate **pelat**, que és el que hi ha dins d'una entrada d'un zip |
| `inflate::zlib(in, in_len, out, cap, written)` | El **wrapper del zlib** (RFC 1950) amb els dos bytes de capçalera i l'Adler-32 — és el que porta un `.woff` |
| `inflate::adler32(data, len, seed)` | El checksum del zlib, que es pot fer en trossos |
| `inflate::crc32(data, len, seed)` | El del zip, per al nom i per als bytes d'una entrada |
| Resultat | `kOk`, `kTruncated`, `kBadData`, `kNoRoom`, `kAdler` |

**El format, en quatre coses que cal saber**

1. **Els bits van del menys significatiu al més significatiu**, però els codis de Huffman es llegeixen al revés
   dins d'aquests bits. És la trampa número u del format i per això el lector té un sol `take()` i un sol
   camí de taula.
2. **Tres menes de bloc**, i les tres són al mateix bucle: `stored` (els bytes tal qual, amb la longitud i el
   seu complement), **taula fixa** (les longituds que el format fixa: 8, 9, 7 i 8 bits pels literals i 5 pels
   distàncies) i **taula dinàmica** (les longituds vénen al stream, comprimides elles mateixes amb una tercera
   taula i el codi 16/17/18 per a les repeticions).
3. **Les taules són canòniques**: es construeixen comptant codis per longitud i posant els símbols en ordre.
   Una taula **sobre-subscrita** no es pot descodificar i es refusa; una d'incompleta també, perquè vol dir
   que al stream li falten codis.
4. **El 258 i el 285**: les longituds de 3 a 258 es diuen amb 29 codis (257 a 285) i els quatre últims porten
   bits extra; les distàncies, amb 30 codis i fins a 13 bits extra.

**Les tres decisions d'aquesta implementació, i per què**

- **Cap reserva pròpia, i la finestra és el buffer d'esortida.** Una distància d'un deflate només mira enrere
  cap al que ja s'ha escrit, o sigui que el bloc on s'escriu *és* la finestra de 32 KB, i no cal una segona.
  Qui crida dóna el bloc i en diu la mida (una entrada de zip la diu a la seva capçalera).
- **Una distància més enllà del començament del que s'ha escrit és un stream trencat** i es refusa amb
  `kBadData`, no es llegeix memòria d'abans del bloc.
- **Les taules són estàtiques i no de la pila**: una taula són 608 bytes i un bloc en fa servir tres, i la pila
  del `loopTask` és de 8 KB (docs/50). Un stream es llegeix a la vegada, com tota la resta de lectors d'aquí.
- **Els dos checksums es poden fer en trossos** (`seed`), que és el que necessita un zip per comprovar una
  entrada mentre la descomprimeix.

**Com es prova**: `test/test_inflate/` amb **bytes que ha comprimit el zlib de debò** i no inventats —la mateixa
frase en bloc dinàmic, en bloc fix i en bloc `stored`, el wrapper amb l'Adler bo i amb un bit canviat, un
stream truncat, un quart tipus de bloc que no existeix, un `stored` que menteix amb la longitud, una sortida
massa petita— i els dos checksums amb els valors de sempre (`adler32("Wikipedia") = 0x11E60398`,
`crc32("123456789") = 0xCBF43926`).

**El que encara no hi és**: el **contenidor zip** (fi del directori central, capçaleres, i el nom de cada
entrada) i la capa d'epub a sobre. El deflate era la peça que no existia; el zip és recórrer estructures.

## Errors típics

| Codi | Quan |
|---|---|
| `E_NOENT` | El camí no hi és, o el volum no està muntat |
| `E_NO_HANDLES` | El pool de 8 és ple |
| `E_TOOBIG` | Un camí o un nom que no hi cap (les lectures ja no tenen sostre, docs/70) |
| `E_UNSUPPORTED` | Operació que el backend no sap fer (afegir a `/nvs`, per exemple) |
| `E_INVAL` | Camí mal format, amb `..` o massa llarg |
| `E_IO` | El backend ha fallat (targeta treta, sector dolent) |

## Desgast de la flash

- LittleFS reparteix les escriptures; tot i així, escriure molt i en bucle es nota.
- Per això el log viu a la RAM i les escriptures d'estat (`fs.write` a `fs.data`) s'han de fer quan
  canvia alguna cosa, no a cada volta del bucle.
- Una app que desa el seu estat dins d'un bucle està desgastant la flash: el que s'ha de desar, es
  desa quan canvia.

## Com es prova

- A l'emulador, cada muntatge és un directori del disc. Es pot mirar amb `ls` i editar amb qualsevol
  editor.
- Les proves natives fan servir volums de memòria RAM per comprovar la resolució de camins, el pool,
  la finestra i els sostres, sense tocar cap disc.

## La imatge de la flash: noms de 32 caràcters com a molt

La imatge de `/littlefs` la fa `tool-mklittlefs` **1.203.210628** (el que instal·la PlatformIO per a
l'ESP32). **Les apps no són a `data/`**: viuen a `apps/`, a l'arrel del projecte, una carpeta per id, i
el disc de cada entorn el munta `tools/make_disk.py` a **`build/disk/<entorn>`** i el fa servir la imatge
d'aquell entorn: `PROJECT_DATA_DIR` és un sol valor per a tot el projecte, i amb un sol `build/disk` el build
de l'ATOM canviava el disc del Paper i el del banc sense que ningú ho hagués demanat. **Cada build té el seu**,
i dins seu la carpeta `apps/` són **enllaços** a `apps/` de l'arrel (una carpeta per app, no un sol enllaç),
o sigui que un entorn pot portar un subconjunt sense copiar res (`custom_apps`). **Les proves són l'excepció
i ho diuen**: creen i esborren apps al disc que munten, i escriure a través d'un enllaç escriuria al
repositori, així que l'entorn `test` demana còpies (`custom_apps_copied = yes`).
Motiu, **mesurat el 3/11** amb la mateixa eina i un bloc de 4 KB: els **113 fitxers** d'`apps/`
necessiten **entre 1 179 648 i 1 310 720 B** de LittleFS, i la partició de l'ATOM, del Matrix i de
l'ESP32 pelat és de **917 504 B**, on l'eina **deixa fora 14 fitxers i tot i així acaba amb exit 0**:
la imatge de tota placa de 4 MB anava curta d'apps i ningú ho deia. El `data/` del sistema (`res/` i
`data/`) hi cap amb lloc de sobres. O sigui que el disc es munta **per placa**: `data/` sola a les de
4 MB —que no tenen apps, i això és aritmètica i no una configuració—, i `data/` més **un enllaç viu** a
`apps/` al Paper (11,9 MB) i al banc, que és el que fa `custom_apps_in_disk = yes`.

**L'enllaç i no una còpia** (mesurat el mateix dia): `mklittlefs` **segueix** un enllaç simbòlic a un
directori i la imatge acaba amb `/apps/hello/hello.lua` a dins, i el banc també (`emu_storage` fa
`stat()` i `S_ISDIR()`, que van al darrere de l'enllaç). Així hi ha un sol lloc on s'edita una app i no
hi ha cap còpia que es quedi vella (docs/85 n'ha estat mossegat dues vegades).

**Mesurat el 30/10** provant l'eina directament sobre directoris de prova a `/tmp`:

| Llargada del nom (amb extensió) | Resultat |
|---|---|
| 32 caràcters | entra a la imatge |
| **33 caràcters** | `unable to open '<camí>.` · `error adding file!` i **el fitxer queda fora** |

Cada **component** del camí compta per separat (cada directori i el fitxer), no el camí sencer: un
camí llarg es pot fer amb directoris niats de 32 caràcters o menys, que és com es prova el selector
d'extensió amb un camí de 100 B (`data/res/this-is-the-first-long-part-here/…`).

**El parany**: `pio run -e m5-atom -t buildfs` (i `uploadfs`) **acaba amb `SUCCESS` encara que hagis
deixat fitxers fora**. El codi de sortida de l'eina és 1 i el seu missatge surt a la consola, però
PlatformIO no el converteix en error. Per això l'entorn `atom` porta `extra_scripts =
pre:tools/check_fs_names.py`, que mira `data/` abans de construir la imatge i **fa fallar l'ordre**
si algun nom passa de 32 caràcters, amb el nom i els caràcters que té. Sense aquest control, el
fitxer simplement no arribava mai a la placa.

Comprovat el 30/10 a la mateixa màquina: un `data/res/aaa…a.txt` de **44 caràcters** fa sortir
`[FAILED]` amb codi 1 i el nom sencer (`44 chars  data/res/…`); esborrat el fitxer, la mateixa ordre
torna a sortir `SUCCESS` amb codi 0 i sense cap línia del control.

En temps d'execució el límit és un altre: el sistema de fitxers refusa camins de 128 B o més
(`path_len >= kMaxPathBytes`), i el nom de cada component depèn del muntatge (`CONFIG_LITTLEFS_OBJ_NAME_LEN`
de l'ESP32, 64 caràcters). El que limita de debò quan es preparen els fitxers és el **32** de
l'eina d'imatge.

## La clau-valor i el parany de la capacitat (31/10)

`IKeyValue::get` demanava la capacitat **dins** del paràmetre de sortida: el cridador havia de posar
la mida del buffer a `len` abans de la crida. El lector del nom de la xarxa va escriure
`size_t len = 0;` i va rebre un «no» silenciós a cada lectura: la placa **desava** el nom i tot seguit
deia `network: none` amb `state: up` (mesurat a la placa, 31/10). Ara la firma és
`get(ns, key, buf, cap, len)` — dues xifres, la capacitat i el que s'ha llegit — i el compilador no
deixa confondre-les. La prova `test_the_key_value_store_keeps_bytes` comprova també que un buffer
sense lloc diu que no.
