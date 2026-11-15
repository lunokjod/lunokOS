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
-- **I la qualitat és `a2`, que és la d'una animació**: dos nivells, sense parpelleig i sense el rentat de setze que un
-- `gc16` fa a cada pas —el primer frame sí que va amb `gc16`, perquè és el que ha de netejar el que hi hagués a sota—.
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

local index = 1
ui.clear(15)
ui.image(x, y, frame_path(index))
ui.present('gc16')

while true do
  index = index % kFrames + 1
  local ok, err = ui.image(x, y, frame_path(index))
  if not ok then
    console.print('loading: ' .. tostring(err))
    break
  end
  ui.present('a2')
  sys.delay(kFrameMs):await()
end
