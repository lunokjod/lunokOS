---
description: "The mandatory `state` session of every app of this system — the shape of the view a person left behind. Use when touching, reviewing, writing or porting any app under apps/, or when an app does not come back as the person left it."
applyTo: "apps/**/*.lua"
---

# La sessió `state` de les apps (norma, 0.189.12)

**I és d'una tirada, amb el son com a excepció** (0.189.167 i 0.189.176): el nucli **esborra el `state` de totes les apps a
cada arrencada de debò**, però **no quan la placa torna d'un son profund** —allà la pàgina ha de ser exactament la que la persona
va deixar, i el `state` és el que la fa tornar—. O sigui que una app que torna a la seva forma ho fa dins de la mateixa encesa o
després d'un son, i mai a través d'un reinici. No hi posis res que hagis de trobar després d'un apagat: això és `fs.data`, no
`state`.

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
   persona no ho trobaria a faltar, és forma. **I el nom del document que l'app té obert tampoc no hi va** (0.192.56): el
   nucli **esborra el `state` a cada arrencada de debò**, o sigui que una pàgina que el llegís tornaria al document per
   defecte —el Paint, que desava el dibuix al seu fitxer i en tornar obria sempre l'esborrany d'un altre dibuix—. Un
   document es recorda en una **sessió pròpia** (`sys.session('status', 256)`, amb el camí a dins), que és el que el nucli
   desa a la targeta abans d'apagar la placa; i si l'app necessita desfer un pas, el que desa és una captura en una altra
   sessió (`undo`) que es llença amb `sys.session_destroy` quan la pàgina marxa, perquè un bloc que viu s'escriu a la
   targeta a cada pausa.
5. **Mira que `sys.session` pugui tornar `nil`** (la memòria era curta): l'app ha de construir la pàgina com si fos el
   primer cop, no petar.
6. **I mira si el bloc que tens desat encara és teu** (0.192.55, i això mata apps): `alive()` és `false` des que l'allocator
   l'ha donat per falta de memòria, i llavors `clear()`, `write()` i `read()` **aixequen `the session memory was given back`**
   i l'app s'acaba. Ho va petar el Paint desant un dibuix —la captura dels píxels demana cent vuitanta kilobytes, o sigui que
   la pressió arriba justament quan deses— i hi havia dotze apps més amb el mateix forat. El guardià és una línia, i va al
   costat de la petició, no a cada crida:

   ```lua
   local function the_state()
     if state_block ~= nil and not state_block:alive() then state_block = nil end
     if state_block == nil then
       local block = sys.session('state', kStateBytes)
       if block == nil then return nil end
       state_block = block
     end
     return state_block
   end
   ```

   I una app que escriu molt de gust el torna a preguntar just abans d'escriure, perquè entre la petició i l'escriptura hi ha
   la feina de la pàgina.

## El que no s'hi posa mai

- Preferències de la persona. Documents, partides i notes (això és dada de l'app, amb el seu fitxer a `fs.data`).
- Cachés que l'app torna a construir llegint el disc: tenen la seva pròpia clau (`apps`, `index`…) i no són `state`.
- Res de maquinari ni de la persona: ni la bateria, ni l'hora, ni el nom.
