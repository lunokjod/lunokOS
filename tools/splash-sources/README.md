# Les imatges font dels moments que vénen del PaperOS

**No són la imatge que el sistema pinta**: són el que es va mirar per copiar el dibuix, i es
queden aquí perquè la còpia es pugui comprovar sense anar a l'altre repositori.

| fitxer | d'on surt |
|---|---|
| `lowbatt-paperos.png` | `~/code/PaperOS/captures/Power off - the battery is low.png`, 540 × 960 i vuit bits: la pantalla que el PaperOS dibuixa amb `drawPowerOffScreen(canvas, PowerOffNotice::BatteryLow)` (`src/ui/power_screen.cpp`, i la insígnia és `drawBatteryLowMark`) |

**De la targeta de càrrega no hi ha cap captura**, i no és un oblit: el `tools/ui_preview.cpp` del
PaperOS només dibuixa la barra amb el cable a dins (`Bar - the battery is charging`), no la targeta
de `src/apps/charging_app.cpp`. Per tant la font d'aquella és **el codi** —`ChargingApp::arrange` i
`::onDraw`, amb el llamp de sis punts de `src/ui/title_bar.cpp` (`fillBolt`)—, i és el que
`tools/make_splash_from_paperos.py` copia número per número.

El resultat de la còpia: `data/res/splash/lowbatt.png` i `data/res/splash/charging.png`, **retallades al
dibuix** (0.183.4): 540 × 456 i 293 × 200. El disseny es dibuixa damunt de la pàgina de 540 × 960 del
PaperOS i es desa com el que ocupa la tinta, que és el que el sistema pinta. La insígnia de la bateria
baixa surt **a 67 píxels** de la captura sobre 11 468 de tinta (0,6 %, i és l'antialiasing de la vora
del triangle): la resta és la mateixa imatge, i la caixa de tinta és la mateixa (251..707 de dalt a
baix). L'única diferència volguda és el nom del sistema, que a la primera línia diu `lunokOS` i no
`PaperOS`.
