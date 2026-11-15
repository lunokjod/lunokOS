-- loading: **una animació al vidre, frame a frame** (0.189.59). El sistema llegeix un GIF i en pinta el primer frame
-- —`lib/img/src/gif.cpp`: una pàgina d'e-ink mostra una imatge i no una animació—, o sigui que una animació aquí és **una
-- seqüència de fitxers i un bucle**, i això és el que aquesta app prova: vint-i-quatre PNG de 500 x 500 a setze nivells de
-- gris, un darrere l'altre cada 140 ms (7,1 fps, el ritme del GIF d'origen).
--
-- **Els frames els escriu `tools/gif_to_frames.py`** i no s'editen a mà: el GIF original és
-- `tools/icons/system/loading_bounce_500x500.gif`, i la conversió ja deixa els setze nivells que el panell té, amb el fons
-- del GIF posat sobre paper.
--
-- **La pàgina no es neteja entre frames**: la imatge cobreix sempre el mateix rectangle i el seu fons és paper, o sigui que
-- el que el vidre ha de tornar a pintar és aquest rectangle i prou. Netejar la vista sencera a cada frame seria demanar-li
-- al panell els 540 x 912 de la pàgina per una imatge de 500 x 500 (docs/30, docs/50).
--
-- **I la qualitat és `du`, el refresc directe de dos nivells**: el dibuix és blanc i negre pur —els PNG són d'un bit, que és
-- el que el GIF té—, i `du` és el mode que no parpelleja i no renta setze nivells que aquesta imatge no fa servir. `a2` i
-- `gc16` són per a una pàgina de setze nivells, i aquí serien demanar-li al panell el doble de feina per res (0.189.60,
-- docs/50, docs/92).
--
-- **I el que mana no és la tinta sinó el cable**: mesurat a l'emulador, un frame de 500 x 500 són **21 ms de descodificar la
-- imatge i 660 ms de presentar-la**, i d'aquests 660 el model del panell en dona 120 a la tinta de `du` i **650 a la
-- transferència** —1 100 µs per fila més el cable de 250 bytes—, que és el que el projecte té mesurat d'aquest vidre (docs/95).
-- O sigui que **set per segon amb 500 x 500 no hi són ni a l'emulador**, i el que fa anar ràpid una animació no és el mode
-- sinó els píxels que s'envien: a 100 x 100 la mateixa transferència són 114 ms i el bucle sí que arribaria al ritme del GIF.
-- Aquesta app envia el GIF sencer perquè és el que es volia veure; una animació de debò es fa petita.
--
-- **AQUESTA APP NO TÉ SESSIÓ `state`** (0.189.52, docs/30): it is a loop that paints frames and nothing else —there is no
-- page a person leaves in a state, no control to focus and nothing to come back to—, so there is no view to record.
-- **I el que es veu a la placa no és el que es veu a l'emulador**: a partial refresh of this panel is some 120 to 300 ms, so
-- seven a second is not something the glass can do; the app asks for the next frame as soon as the last one is presented and
-- the panel sets the pace (docs/50, docs/85).

local caps = sys.caps()
if not caps.display then
  console.print('loading: this board has no glass')
  return
end

-- El ritme del GIF d'origen: cada frame dura 140 ms.
local kFrameMs = 140
local kFrames = 24

local function frame_path(index)
  return string.format('/res/anim/f%02d.png', index)
end

local info, why = ui.image_info(frame_path(1))
if not info then
  console.print('loading: ' .. tostring(why))
  return
end

-- La imatge va **centrada a la vista**, que és el que queda sota la barra del sistema.
local view = ui.caps()
local x = math.floor((view.width - info.width) / 2)
local y = math.floor((view.height - info.height) / 2)
if x < 0 then x = 0 end
if y < 0 then y = 0 end

-- **La pàgina surt neta i el primer frame no ha de rentar res**: `du` pinta els dos nivells del rectangle i prou.
local index = 1
ui.clear(15)
ui.image(x, y, frame_path(index))
ui.present('du')

while true do
  index = index % kFrames + 1
  local ok, err = ui.image(x, y, frame_path(index))
  if not ok then
    console.print('loading: ' .. tostring(err))
    break
  end
  ui.present('du')
  sys.delay(kFrameMs):await()
end
