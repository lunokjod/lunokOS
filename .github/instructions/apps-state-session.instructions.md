---
description: "The mandatory `state` session of every app of this system — the shape of the view a person left behind. Use when touching, reviewing, writing or porting any app under apps/, or when an app does not come back as the person left it."
applyTo: "apps/**/*.lua"
---

# La sessió `state` de les apps (norma, 0.189.12)

**Tota app amb pàgina té `sys.session('state', 1024)` i és el que fa que la vista torni exactament com la va deixar la
persona**: el control que tenia el cercle, la pàgina d'una llista o d'una graella, la pestanya triada, les caselles
marcades, el contingut de les caixes de text i fins on era desplaçada una caixa llarga. El contracte sencer —format,
codi que es copia, quan es desa i quan es llegeix— és a **`docs/30-api-lua.md` § «La sessió `state`»**; la llista de què
s'hi ha de mapejar en portar una app és a **`docs/45-portar-una-app-del-paper.md` § «5 bis»**.

## El que has de fer sempre que toquis el codi d'una app

1. **Repassa la sessió `state`** encara que el canvi no hi sembli relacionat: llegeix el bloc a `build()` **abans** de
   col·locar res, desa'l a cada canvi de forma i també a `on_pause`, `on_save` i `on_close`.
2. **Valora el que llegeixes** abans de fer-lo servir: `tonumber` i acota la pàgina i la pestanya a les que existeixen, i
   posa el focus en un control que hi sigui; un control que no existeix no és un focus, és una pàgina sense cercle.
3. **Si un control canvia de nom, se'n treu un o n'arriba un de nou, puja `kLayout`** (`v1` → `v2`). No migris mai una
   forma antiga cap a una de nova: un estat d'una altra forma es llença.
4. **El bloc no pot fer res que no sigui recordar la forma**: si el que hi vols posar és una decisió de la persona (tema,
   unitat, idioma, ordre), va a `/cfg` o a `fs.data`, no a `state`. La prova: si l'app ho pot decidir tota sola i la
   persona no ho trobaria a faltar, és forma.
5. **Mira que `sys.session` pugui tornar `nil`** (la memòria era curta): l'app ha de construir la pàgina com si fos el
   primer cop, no petar.

## El que no s'hi posa mai

- Preferències de la persona. Documents, partides i notes (això és dada de l'app, amb el seu fitxer a `fs.data`).
- Cachés que l'app torna a construir llegint el disc: tenen la seva pròpia clau (`apps`, `index`…) i no són `state`.
- Res de maquinari ni de la persona: ni la bateria, ni l'hora, ni el nom.
