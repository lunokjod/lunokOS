-- handwrite: **la pàgina que ensenya el teclat la mà d'aquesta persona** (0.192.71, l'amo: *«Aquest teclat necessita una app d'
-- entrenament. Aquesta app treu el teclat i fa escriure paraules a l'usuari per obtenir els caràcters un per un per entrenar el
-- perceptrón … l'usuari dibuixa una lletra · Esperar 1.2 segon · Passar-li al perceptron en mode entrenament la cuadrícula 5x5 ·
-- Desar l'estat del perceptron i el caràcter a un arxiu que el teclat pugui usar»*).
--
-- **El que aprèn és del teclat i no d'aquesta pàgina**: the model lives in the kernel (`hand.*`), the band of the keyboard reads
-- the same file, and this page is the one that fills it. O sigui que el que aquesta pàgina ensenya és el que el teclat escriu al
-- següent caràcter que es dibuixa, sense reiniciar res (docs/30, docs/90).
--
-- **I el que fa és el flux de l'amo, amb una cosa més**: after the 1.2 seconds the drawing is *shown as the grid the model sees*
-- —seven rows of cells with three levels— perquè una persona ha de poder veure que el que ha dibuixat és el que s'ha après; un
-- traç dolent es veu aquí i es torna a dibuixar, i no més tard quan el teclat escriu una altra lletra (docs/30, docs/45).
--
-- **La feina va en una llista de caràcters per zona** i no en paraules: the keyboard needs every letter of the alphabet before it
-- can write a sentence, o sigui que la pàgina porta el seu propi ordre i la paraula que es ve a sota és un record i no el camí
-- (una paraula que ensenyi només les lletres que hi surten deixa mitja llengua sense entrenar). Les lletres amb accent són bytes
-- solts, com a tot aquest sistema (docs/30).

local caps = sys.caps()
if not caps.display then
  console.print('handwrite: this board has no glass')
  return
end
if hand == nil then
  console.print('handwrite: this build has no hand model')
  return
end

-- **La forma de la vista** i prou: el caràcter on s'és no és la forma d'una pàgina —és la feina d'aquesta app, i ha de sobreviure
-- a un reinici—, o sigui que va al fitxer de dades i no a `state` (docs/30, docs/45).
local kLayout = 'v1'
local kStateBytes = 256
local kDataLayout = 'v1'

-- **Els números del flux** (l'amo: *«Esperar 1.2 segon»*): la pausa que diu que la lletra s'ha acabat, quantes mostres vol cada
-- caràcter abans de donar-lo per après, i el pas del bucle.
local kRestMs = 1200
local kGoal = 3
-- **El pas del bucle, dos mil·lisegons** (0.1.17): the chip hands a frame over every ten, and it is the turn of the app that makes
-- the kernel read the glass —o sigui que un pas de quaranta mil·lisegons perdia el començament de cada traç curt, que és el que fa
-- que un accent sigui un punt—. Dos mil·lisegons són cinc preguntes per marc del xip i prou perquè el sistema atengui la resta
-- (docs/30, docs/50).
local kStepMs = 2
-- **El paper no es toca fins que la lletra s'ha acabat** (0.192.72, l'amo: *«NOOO... NO DIBUIXIS FINS AL FINAL, quan l'usuari ha
-- llevat el dit durant > 1.2 segons»*): el dit apunta punts i no es pinta res; quan la pausa passa, la lletra sencera apareix d'un
-- cop. És la regla del Paint —una pàgina de tinta que es va omplint a cada pas del dit és un martelleig i deixa el fantasma de
-- cada intent— i aquí encara compta més: el que es dibuixa és el que el model ha de veure, i el que s'ensenya a mitges no és això.
-- **El bolígraf, molt més gruixut** (0.192.74, l'amo: *«La pintada de la lletra quan es lleva el dit amb un gruix molt, molt MOLT
-- MES gros»*): the pen went from seven pixels to **twenty-one** —a radius of ten, a square of twenty-one at every step of the
-- path—, which on this glass is the width of a marker and not the width of a finger's edge (docs/30, docs/90).
local kPenRadius = 10

-- **I el recuadre del paper i el de la quadrícula, gruixut i negre** (l'amo: *«No queda gens clar el recuadre, es massa claret i
-- fi, fes-lo mes gruixut»*): a one-pixel frame in the middle grey was a frame a person had to look for; what is drawn now is six
-- pixels of ink, one frame inside the other (docs/30, docs/85).
local kFramePx = 6


-- **Els caràcters d'una zona, en l'ordre en què s'ensenyen**: the letters of Catalan —the twenty-six and the eight with a mark,
-- which are one byte each in this system— i els símbols de la segona zona, que és la que la banda ensenya a l'altre panell.
local kAccented = string.char(0xE0, 0xE8, 0xE9, 0xED, 0xF2, 0xF3, 0xFA, 0xE7)  -- à è é í ò ó ú ç
local LETTERS = 'abcdefghijklmnopqrstuvwxyz' .. kAccented
local SYMBOLS = '0123456789+-=/()*.,:;!?'

-- **Una paraula per fer de record** (l'amo: *«fa escriure paraules a l'usuari»*): the first word that carries the character is what
-- the page shows under it. All of them are ASCII —a word is a hint and not a lesson— i el caràcter sense paraula es queda amb ell
-- mateix.
local kWords = {
  'mar', 'bo', 'casa', 'dit', 'el', 'fi', 'gat', 'hi', 'illa', 'joc', 'kiwi', 'la', 'mar', 'nou', 'os', 'pa', 'qui',
  'riu', 'sol', 'tal', 'un', 'vi', 'wifi', 'xarxa', 'yoga', 'zoo',
}

local function a_word_for(c)
  for i = 1, #kWords do
    if kWords[i]:find(c, 1, true) then return kWords[i] end
  end
  return c
end

local MARGIN = 16
local GAP = 10
local ROW_H = 58

-- **Les caixes dels textos es compten amb la cara del sistema i no es suposen** (0.192.79, l'amo: *«La paraula amb "p" o "q" o "g" o
-- "j" surten tallades perqué les dibuixes massa a la vora del panell on s'escriu»*): the outline face of this disk is eleven pixels a
-- step and the `5x7` of the flash is seven, o sigui que una caixa de trenta-quatre píxels per a un text de quatre passos tallava
-- les cames de la `p`, la `q`, la `g` i la `j`. Es pregunta a `ui.line_height`, que és el número que el nucli gasta per pintar-lo.
local kTitleScale = 5
local kSubScale = 4
local kHintScale = 4
local kFootScale = 3
-- **La mida del caràcter, set passos i no més** (0.192.79, trobat mesurant): una cara d'outlines rasteritza cada glif dins d'un
-- bloc de **seixanta-quatre píxels** (`kOutlineMaxWidth`/`kOutlineMaxHeight`, `font.cpp`), i un glif que no hi cap **no es pinta i no
-- diu res**: `kOutlineBody` és nou, o sigui que a deu passos el cos fa noranta píxels i la lletra desapareixia —les curtes com `a` o
-- `+` sí que hi cabien, i per això el forat es veia en unes lletres i no en unes altres—. A set passos el cos fa seixanta-tres i hi
-- caben totes, comptades una per una amb `--dump` a l'emulador (docs/30, docs/85).
local kCharScale = 7
local kPad = 8

local title_h, sub_h, hint_h, foot_h, char_h = 52, 34, 34, 34, 96
local grid_side, grid_h = 168, 180
local paper_max = 280

-- **Dues vistes**: la pàgina que dibuixa i la del model (0.192.86, l'amo: *«Afegeix un botó a l'app d'entrenament. En una vista
-- separada, estil "Settings" (usa una icona de roda dentada per anar a la vista on està el botó d'esborrar)»*). La segona és on es
-- veu què ha après el model, caràcter a caràcter, i on es pot esborrar el que està malament (docs/30, docs/45).
local mode = 'draw'         -- 'draw' or 'model'
-- **Set línies de comptes**: a three steps five pairs of a character fit in this width, i trenta-quatre caràcters són set línies de
-- cinc; una llista que no hi cap s'acaba amb tres punts i no diu res (docs/30).
-- **Set caràcters per fila**, que són les cinc files de trenta-quatre caràcters: the cell is as wide as the page allows and as tall as
-- it is wide, que és el que fa una tecla d'una graella (docs/30).
-- **La llista del model es declara aquí i es crea a la vista**: `layout` és a mig fitxer i sense aquesta línia veuria una global
-- buida, o sigui que la llista no es col·locava i es quedava a la cantonada (docs/30).
local the_list = nil
-- **El «Forget all» demana dos tocs**: the first one arms it and says so, i el segon esborra (docs/30).
local forget_all_armed = false
local gear, forget_button, forget_all_button = nil, nil, nil
-- **Una vista nova no és un repintat**: `needs_a_paint` torna a pintar els controls de la pàgina que hi ha, i canviar de vista vol
-- dir que els controls són uns altres; aquest senyal mana el camí sencer (`rebuild_the_page`) des del torn (docs/30, docs/90).
local needs_a_rebuild = false

local function measure_the_text_boxes()
  title_h = ui.line_height(kTitleScale) + kPad
  sub_h = ui.line_height(kSubScale) + kPad
  hint_h = ui.line_height(kHintScale) + kPad
  foot_h = ui.line_height(kFootScale) + kPad
  char_h = ui.line_height(kCharScale) + kPad
end

-- ------------------------------------------------------------------ la forma i les dades

local state_block = nil
local shape = { zone = 'letters' }

-- **El bloc que es pregunta sempre si encara és meu** (0.192.55, docs/30): el que l'allocator s'ha quedat és `alive()` fals i
-- tocar-lo acaba l'app.
local function the_state()
  if state_block ~= nil and not state_block:alive() then state_block = nil end
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function save_state()
  local block = the_state()
  if block == nil then return end
  -- The layout goes in the first line and nothing else: the character being taught is work and not shape, and it lives in the
  -- app's own file (docs/45).
  block:clear()
  block:write(0, kLayout .. '\n')
end

local function load_state()
  local block = the_state()
  if block == nil then return end
  local text = block:read(0, kStateBytes) or ''
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  if (text:match('^([^\n]*)') or '') ~= kLayout then return end
end

-- **On s'és de la feina**: `v1 zone index`, al fitxer de dades de l'app —el seu `/data/handwrite/`, que és l'únic lloc on pot
-- escriure i el mateix on el teclat llegeix el model— (docs/70).
local work = { zone = 'letters', index = 1, testing = false, hits = 0, tries = 0 }

-- **Si hi ha alguna cosa nova per desar** (0.192.78, l'amo: *«I el botó "save" si no hi ha res a salvar?»*): the model is written
-- when the app closes anyway, i el botó només té feina quan s'ha après alguna mostra que encara no és al disc (docs/30, docs/70).
local dirty = false

local function the_data_path()
  local base = fs.data()
  if base == nil then return nil end
  fs.mkdir(base)
  return base .. '/progress'
end

local function load_work()
  local path = the_data_path()
  if path == nil then return end
  local text = fs.read(path)
  if type(text) ~= 'string' then return end
  -- **El fitxer es llegeix amb el salt de línia** (0.192.85): `save_work` escriu la línia amb un `\n` al final i el patró demanava
  -- el final just després de l'últim número, o sigui que **no hi havia cap reinici que llegís on s'era** i la pàgina tornava sempre a
  -- la primera lletra. El patró s'atura al número i prou (docs/30, docs/70).
  local layout, zone, index, testing = text:match('^(%S+) (%S+) (%d+) (%d+)')
  if layout ~= kDataLayout then return end
  if zone == 'letters' or zone == 'symbols' then work.zone = zone end
  work.index = math.max(1, tonumber(index) or 1)
  work.testing = (testing == '1')
end

local function save_work()
  local path = the_data_path()
  if path == nil then return end
  fs.write(path, string.format('%s %s %d %d\n', kDataLayout, work.zone, work.index, work.testing and 1 or 0))
end

-- ------------------------------------------------------------------ el que s'aprèn

local function the_characters()
  return (work.zone == 'letters') and LETTERS or SYMBOLS
end

local function the_character_at(index)
  local list = the_characters()
  if index < 1 then index = 1 end
  if index > #list then return nil end
  return list:sub(index, index)
end

-- **Quantes vegades s'ha ensenyat un caràcter**, preguntat al model i no comptat aquí: el model és qui ho sap, i una pàgina que
-- porti el seu propi compte es desincronitza el dia que el fitxer ve d'una altra banda (docs/30).
local function taught(c)
  local n = hand.count(work.zone, c)
  return (type(n) == 'number') and n or 0
end

local function taught_total()
  local list = the_characters()
  local done, sum = 0, 0
  for i = 1, #list do
    local n = taught(list:sub(i, i))
    sum = sum + n
    if n >= kGoal then done = done + 1 end
  end
  return done, sum, #list
end

-- **El primer caràcter que encara no té les seves mostres**: where the page goes next, que és el que fa que una sessió
-- d'entrenament no s'hagi de recordar on va quedar.
local function the_first_missing()
  local list = the_characters()
  for i = 1, #list do
    if taught(list:sub(i, i)) < kGoal then return i end
  end
  return nil
end

-- ------------------------------------------------------------------ la pàgina

local title, subtitle, big, hint, paper, grid_canvas, retry, skip, zone_button, save_button, footer
local owned = {}
local wid, hei = 0, 0
local paper_x, paper_y = 0, 0
local grid_x, grid_y = 0, 0

-- **El dibuix que s'està fent**: una llista de traços i, a dins de cada un, els punts en píxels del paper. És el que es passa a
-- `hand.*` i el que es pinta a la pàgina, i no hi ha cap còpia (docs/75).
local strokes = {}
local last_grid = nil
-- **La línia que s'està rebent** (0.1.17): the kernel numbers the lines of the capture and a point whose number is not the one
-- being built opens a line of its own —a second contact is a second stroke and not a jump of the first— (docs/30, docs/50).
local the_line = nil
-- **Si el paper té la lletra d'abans**: the ink is only put there when a letter is finished, i la lletra següent comença en un
-- paper net (docs/30, docs/85).
local paper_is_dirty = false
-- **I el que hi ha pintat al paper**: `strokes` es buida quan la lletra es tanca —ja és del model i de la quadrícula—, o sigui que
-- el paper necessita la seva pròpia còpia o el repintat següent el deixava **buit** i la lletra desapareixia just després d'aparèixer
-- (0.1.15, l'amo: *«perqué no es dibuixa el traç al panell del teclat d'entrenament?»*) (docs/30, docs/85).
local paper_strokes = nil
-- **Esborrar tinta no és pintar-la** (0.1.18, l'amo: *«Ara al handwrite es veu la meva traçada però no l'esborren»*): the page was
-- presented with the grey level of a normal paint (`gld16`), which on this glass **leaves the trace** of the ink that was there —a
-- `gld16` is meant to change one grey into another and not to clean it—, o sigui que la lletra es veia a sota com un fantasma i
-- semblava que no s'esborrés mai. El que neteja és un `gc16`, i es demana quan de veritat s'ha tret tinta del paper (docs/30,
-- docs/50, docs/85).
local the_paper_wants_a_clean = false

local function erase_the_paper()
  if paper_strokes ~= nil then the_paper_wants_a_clean = true end
  paper_strokes = nil
  paper_is_dirty = false
end

local function present_the_page()
  if the_paper_wants_a_clean then
    the_paper_wants_a_clean = false
    ui.present('gc16')
    return
  end
  ui.present()
end

-- **El rellotge de la pausa**: `finger_is_down` diu si hi ha un dit **al paper** i `rest_from` quan se'n va anar l'últim (docs/30).
local finger_is_down = false
local rest_from = nil

-- **La captura del paper** (0.1.17, l'amo: *«Captura tots els gestos al panell d'entrenament i converteix-lo en línia … per no
-- perdre línies, per exemple, sense això els accents semblen un punt sobre el símbol»*): the page does not read the panel in its
-- turn —a frame the kernel saw and nobody took is a piece of a line that is gone for ever, and the mark above an `à` is two or
-- three frames—; it asks the kernel for the box of the paper and the kernel turns every frame that lands there into a point of a
-- line, saying where the line starts and where it ends (docs/30, docs/50).
local function arm_the_paper()
  if mode ~= 'draw' or paper == nil then
    touch.capture()
    return
  end
  the_line = nil
  finger_is_down = false
  rest_from = nil
  touch.capture(paper_x, paper_y, wid - 2 * MARGIN, paper_max)
end

-- **El que un botó demana es pinta al torn de l'app i no dins del botó** (0.192.79): a hook —the click of a button— runs between two
-- turns, i el sistema no accepta dibuix en aquell moment: el que s'apunta és que hi ha feina i `one_turn` la fa amb la pàgina al
-- davant. És el mateix camí que el traç i el model, que sí que arriben al vidre (docs/30, docs/50).
local needs_a_paint = false

local function keep(control)
  if control ~= nil then owned[#owned + 1] = control end
  return control
end

local function give_back_the_view()
  for i = #owned, 1, -1 do
    local one = owned[i]
    if one ~= nil then pcall(function() one:release() end) end
  end
  owned = {}
  title, subtitle, big, hint, paper, grid_canvas = nil, nil, nil, nil, nil, nil
  retry, skip, zone_button, save_button, footer = nil, nil, nil, nil, nil
  gear, forget_button, forget_all_button = nil, nil, nil
  forget_all_armed = false
  the_list = nil
end

-- **Un traç gruixut i no una línia** (0.192.72, l'amo: *«i fes-ho amb un traç gruixut, no una linia»*): `cv:line` d'un píxel és la
-- vora del que un dit deixa i no el que deixa; el que es pinta és un quadrat de set píxels (radi tres) a cada pas del camí, que és
-- el dibuix d'un bolígraf gruixut d'aquest vidre (docs/30, docs/90).
local function a_wide_dot(canvas, x, y)
  canvas:rect(x - kPenRadius, y - kPenRadius, 2 * kPenRadius + 1, 2 * kPenRadius + 1, 0)
end

-- **Un recuadre que es veu, i amb la cantonada lleugerament rodona** (0.192.79, l'amo: *«Fes les vores lleugerament arrodonides del
-- panell»*): `cv:frame` fa un píxel de gruix i no té radi, o sigui que el marc es camina a mà —quatre costats gruixuts i, a cada
-- cantonada, una fila per graó amb el retrocés que dóna el cercle— i el resultat és una targeta i no una caixa d'un pixel de vora
-- (docs/30, docs/85).
local kCornerRadius = 12

local function a_rounded_frame(canvas)
  local box = canvas:caps()
  local w, h = box.width, box.height
  local t = kFramePx
  local r = math.min(kCornerRadius, math.min(w, h) // 4)
  if r < 2 then
    for i = 0, t - 1 do canvas:frame(i, i, w - 2 * i, h - 2 * i, 0) end
    return
  end
  canvas:rect(r, 0, w - 2 * r, t, 0)
  canvas:rect(r, h - t, w - 2 * r, t, 0)
  canvas:rect(0, r, t, h - 2 * r, 0)
  canvas:rect(w - t, r, t, h - 2 * r, 0)
  for i = 0, r - 1 do
    local dy = r - i - 0.5
    local inset = r - math.floor(math.sqrt(r * r - dy * dy) + 0.5)
    canvas:rect(inset, i, t, 1, 0)
    canvas:rect(w - inset - t, i, t, 1, 0)
    canvas:rect(inset, h - 1 - i, t, 1, 0)
    canvas:rect(w - inset - t, h - 1 - i, t, 1, 0)
  end
end

local function paint_a_stroke(canvas, stroke)
  if #stroke == 0 then return end
  a_wide_dot(canvas, stroke[1][1], stroke[1][2])
  for i = 2, #stroke do
    local x0, y0 = stroke[i - 1][1], stroke[i - 1][2]
    local x1, y1 = stroke[i][1], stroke[i][2]
    local steps = math.max(math.abs(x1 - x0), math.abs(y1 - y0))
    if steps < 1 then
      a_wide_dot(canvas, x1, y1)
    else
      for k = 0, steps do
        a_wide_dot(canvas, x0 + (x1 - x0) * k // steps, y0 + (y1 - y0) * k // steps)
      end
    end
  end
end

-- **El paper sencer, quan la lletra ja està feta**: the paper is cleaned, its frame is put back and the strokes are painted thick,
-- tot d'un cop i amb una sola passada al vidre (docs/30, docs/85).
local function paint_the_strokes(list)
  if paper == nil then return end
  local drawn = list or {}
  paper:clear(15)
  -- **El paper s'ha de veure** (0.192.71, i gruixut des de la 0.192.74): a white area on a white page is a page that does not say
  -- where the finger draws.
  a_rounded_frame(paper)
  for _, stroke in ipairs(drawn) do
    paint_a_stroke(paper, stroke)
  end
  paper:invalidate()
end

-- **El paper amb el que hi ha pintat** (0.1.15): a redraw of the page must put back the letter that is there, i no el que s'està
-- dibuixant ara —que després de tancar la lletra ja és buit— (docs/30, docs/85).
local function paint_the_paper()
  paint_the_strokes(paper_strokes or {})
end

-- **I la quadrícula que el model veu**, que és el que fa confiar-hi: seven rows of seven cells with three tones, the stroke ends
-- included in the two things the cells cannot say —the number of strokes and the shape of the box— (docs/30, docs/90).
local function paint_the_grid()
  if grid_canvas == nil then return end
  grid_canvas:clear(15)
  local box = grid_canvas:caps()
  a_rounded_frame(grid_canvas)
  if last_grid == nil then
    grid_canvas:invalidate()
    return
  end
  -- **La cel·la és la que el panell té i no una de sortida**: the canvas is exactly seven cells of `grid_side` and the frame, o
  -- sigui que no hi ha espai mort sota la graella sigui quina sigui la mida del vidre (docs/30).
  local cell = grid_side
  local side = cell * 7
  local x0 = (box.width - side) // 2
  local y0 = (box.height - side) // 2
  for y = 0, 6 do
    for x = 0, 6 do
      local level = tonumber(last_grid.cells:sub(y * 7 + x + 1, y * 7 + x + 1)) or 0
      local ink = (level == 0) and 15 or ((level == 1) and 4 or 0)
      grid_canvas:rect(x0 + x * cell, y0 + y * cell, cell - 1, cell - 1, ink)
    end
  end
  grid_canvas:invalidate()
end

-- **Un botó que no pot fer res, deshabilitat** (0.192.77, l'amo: *«Si no puc polsar el botó perquè no hi ha traç, deshabilita»*):
-- `Retry` buida el paper i la quadrícula, i amb el paper net no té res a esborrar; el sistema el pinta amb la tinta del `dim` i no
-- respon ni al dit ni al dial (docs/30).
-- **Un botó que no té res a fer, apagat, i els dos alhora** (0.192.82, l'amo: *«El botó Retry no hauría d'estar actiu si Save acaba de
-- guardar i està deshabilitat»*): `Retry` llença l'intent que hi ha al paper, i un intent que ja és al disc no és un intent —o sigui
-- que la mateixa condició que apaga `Save` apaga `Retry`: hi ha feina quan s'està dibuixant (`#strokes > 0`) o quan hi ha una mostra
-- que encara no és al disc (`dirty`). Amb el model desat els dos diuen «ara no» i la pàgina no demana res (docs/30).
local function update_the_buttons()
  -- **Un dibuix que ja hi era també és feina a la mà** (0.1.18, l'amo: *«es veu la meva traçada però no l'esborren»*): a drawing
  -- that the model already had adds nothing and leaves `dirty` at no, o sigui que `Retry` i `Save` es quedaven tots dos apagats amb
  -- la lletra ben viva al paper i no hi havia cap botó que la tragués. El que hi ha pintat al paper compta com el que s'està
  -- dibuixant (docs/30, docs/85).
  local work_in_hand = (#strokes > 0) or dirty or (paper_strokes ~= nil)
  if retry ~= nil then retry.disabled = not work_in_hand end
  if save_button ~= nil then save_button.disabled = not dirty end
end

local function say_the_work()
  if footer == nil then return end
  local done, sum, all = taught_total()
  local c = the_character_at(work.index)
  if work.testing then
    local pct = (work.tries > 0) and math.floor(work.hits * 100 / work.tries + 0.5) or 0
    footer.text = string.format('test: %d of %d right (%d%%)', work.hits, work.tries, pct)
  elseif c == nil then
    footer.text = string.format('done: %d of %d letters, %d example(s)', done, all, sum)
  else
    -- **El peu, en ASCII** (0.192.77, l'amo: *«el text útil d'abaix … desapareix dient xorrades»*): the middle dot of this line
    -- was two bytes of UTF-8 and this glass reads one byte a character, o sigui que es pintava com dues lletres i la línia es
    -- feia més llarga i es tallava. El que hi ha ara cap i diu el mateix: el caràcter, les seves mostres i quantes en falten.
    footer.text = string.format('%s %d/%d  %d left', c, taught(c), kGoal, all - done)
  end
end

local function layout()
  local y = MARGIN
  if mode == 'model' then
    -- **La fletxa a l'esquerra i el títol seguit** (0.192.86, l'amo: *«la fletxa a l'esquerra home! i el titol seguit»*): the corner
    -- of a page of this house is the way out and the name goes right after it, que és el que fa una pàgina que torna (docs/30, docs/90).
    if gear ~= nil then gear:place(MARGIN, y + (title_h - 44) // 2) end
    if title ~= nil then
      local left = MARGIN + 48 + 10
      title:resize(wid - left - MARGIN, title_h)
      title:place(left, y)
    end
    y = y + title_h
    if subtitle ~= nil then
      subtitle:resize(wid - 2 * MARGIN, sub_h)
      subtitle:place(MARGIN, y)
    end
    y = y + sub_h + GAP
    if the_list ~= nil then
      the_list:resize(wid - 2 * MARGIN, list_h)
      the_list:place(MARGIN, y)
    end
    y = y + list_h + GAP
    if forget_button ~= nil then
      forget_button:resize(wid - 2 * MARGIN, ROW_H - 8)
      forget_button:place(MARGIN, y)
    end
    y = y + ROW_H
    if forget_all_button ~= nil then
      forget_all_button:resize(wid - 2 * MARGIN, ROW_H - 8)
      forget_all_button:place(MARGIN, y)
    end
    y = y + ROW_H + GAP
    if footer ~= nil then
      footer:resize(wid - 2 * MARGIN, foot_h)
      footer:place(MARGIN, y)
    end
    return
  end
  if title ~= nil then
    title:resize(wid - 2 * MARGIN, title_h)
    title:place(MARGIN, y)
  end
  if gear ~= nil then gear:place(wid - MARGIN - 48, 4) end
  y = y + title_h
  if subtitle ~= nil then
    subtitle:resize(wid - 2 * MARGIN, sub_h)
    subtitle:place(MARGIN, y)
  end
  y = y + sub_h + GAP
  if big ~= nil then
    big:resize(wid - 2 * MARGIN, char_h)
    big:place(MARGIN, y)
  end
  y = y + char_h
  if hint ~= nil then
    hint:resize(wid - 2 * MARGIN, hint_h)
    hint:place(MARGIN, y)
  end
  y = y + hint_h + GAP
  if paper ~= nil then
    paper_x, paper_y = MARGIN, y
    paper:place(MARGIN, y)
  end
  y = y + paper_max + GAP
  if grid_canvas ~= nil then
    grid_x, grid_y = MARGIN, y
    grid_canvas:place(MARGIN, y)
  end
  y = y + grid_h + GAP
  local third = (wid - 2 * MARGIN - 3 * GAP) // 4
  if retry ~= nil then retry:place(MARGIN, y) end
  if skip ~= nil then skip:place(MARGIN + third + GAP, y) end
  if zone_button ~= nil then zone_button:place(MARGIN + 2 * (third + GAP), y) end
  if save_button ~= nil then save_button:place(MARGIN + 3 * (third + GAP), y) end
  y = y + ROW_H + GAP
  if footer ~= nil then
    footer:resize(wid - 2 * MARGIN, foot_h)
    footer:place(MARGIN, y)
  end
end

-- **La pàgina sencera quan el caràcter canvia**: the big character and the word that carries it, the paper clean and the grid of
-- the last drawing still there —el que s'acaba de fer és el que es mira— (docs/30).
local function show_the_character()
  local c = the_character_at(work.index)
  if c == nil then
    if big ~= nil then big.text = 'done' end
    if hint ~= nil then hint.text = 'every character has its examples: the keyboard can write' end
  else
    if big ~= nil then big.text = c end
    if hint ~= nil then hint.text = a_word_for(c) end
  end
  say_the_work()
  update_the_buttons()
end

local function the_glyph_is_done()
  if #strokes == 0 then return end
  local drawn = strokes
  strokes = {}
  the_line = nil
  -- **La lletra apareix ara i sencera** (l'amo: *«NO DIBUIXIS FINS AL FINAL»*): el que s'ha apuntat mentre el dit hi era es pinta
  -- d'un cop, amb el gruix del bolígraf, i el que queda al paper és el que el model ha vist (docs/30, docs/85).
  -- **I es queda al paper** (0.1.15): `strokes` ja és buit quan el torn repinta la pàgina, o sigui que el que s'ha de tornar a
  -- pintar és aquesta còpia i no la llista viva (docs/30).
  paper_strokes = drawn
  paper_is_dirty = true
  -- **El camp on s'ha dibuixat** (0.195.0, l'amo: *«no detecta bé els símbols… VUll que m'entengui»*): el paper de la pàgina, i no
  -- la pàgina sencera. És el que diu al model **on** cau el traç i **quant n'ocupa**, i sense això un `-` i un `_` són el mateix
  -- dibuix i un `.` és una ratlla. Les tres crides el porten, o el model apren i compara una cosa i n'escriu una altra (docs/30,
  -- docs/90).
  local field_w, field_h = wid - 2 * MARGIN, paper_max
  local grid = hand.grid(drawn, field_w, field_h)
  if grid == nil then
    -- **Un toc no és una lletra, i no es diu res** (0.192.77, l'amo: *«no molestis al usuari amb estupideses»*): the drawing was a
    -- tap or had no shape, o sigui que s'esborra i el peu es queda amb el que deia —el caràcter, les seves mostres i les que
    -- falten—, que és el que una persona ha de llegir (docs/30).
    paper_strokes = nil
    paint_the_paper()
    present_the_page()
    return
  end
  last_grid = grid
  paint_the_grid()
  local c = the_character_at(work.index)
  if c ~= nil then
    if work.testing then
      local answer = hand.guess(work.zone, drawn, field_w, field_h)
      work.tries = work.tries + 1
      if answer ~= nil and answer.text == c then
        work.hits = work.hits + 1
        if footer ~= nil then footer.text = 'right: ' .. c end
      elseif answer ~= nil then
        if footer ~= nil then footer.text = 'wrong: it read ' .. answer.text .. ' and was ' .. c end
      else
        if footer ~= nil then footer.text = 'wrong: it read nothing and was ' .. c end
      end
    else
      local added = hand.add(work.zone, c, drawn, field_w, field_h)
      if added then dirty = true end
      if footer ~= nil then
        footer.text = added and ('learned ' .. c .. ' (' .. taught(c) .. ' of ' .. kGoal .. ')')
          or ('the same drawing of ' .. c .. ' was already there')
      end
      -- **El caràcter no es mou sol** (0.192.85, l'amo: *«Perqué quan acabo de dibuixar un caràcter CANVIA EL PUTO CARACTER en quant
      -- llevo el dit?? … VOLS FER EL FAVOR DE NO CANVIAR DE SIMBOL FINS QUE LI DONI A SKIP O SAVE?»*): the page used to jump to the
      -- next character the moment the third example went in —and with a character that already had its three, **every** drawing
      -- jumped—, and a person who lifts a finger to look at what they drew found another letter and no way to know which one the
      -- sample had gone to. Ara la lletra es queda fins que es prem `Skip` (el següent) o `Save` (desa i, si aquesta ja les té
      -- totes, passa a la primera que en falta) (docs/30).
      if footer ~= nil and taught(c) >= kGoal then
        footer.text = string.format('%s %d/%d  Skip for the next one', c, taught(c), kGoal)
      end
    end
  end
  save_work()
  -- **La pàgina no es torna a fer per cada lletra, i el que s'acaba de dibuixar-s'hi queda**: the labels change, the grid is the
  -- one of this drawing and the paper keeps the letter —el que la persona ha fet és el que ha de poder mirar— fins que en comença
  -- una altra (docs/30, docs/85).
  needs_a_paint = true
end

-- ------------------------------------------------------------------ la vista

-- **La pàgina sencera es demana des de quatre llocs i es defineix més avall**: with a forward local the closure of a button sees the
-- one function and not a global that is nil when the button is built (docs/30).
local rebuild_the_page

-- **El que el model té, en una llista** (0.192.86, l'amo: *«Pots fer l'interficie amb menys ganes? una llista per déu!»*): the seven
-- crammed lines of text were a table nobody designed; what the system has for this is a list —a row per character, its examples on
-- the right, the paginator of the system under it and the finger choosing—, and the row that is chosen is the one `Forget` takes
-- away (docs/30, docs/45).
-- **Una graella i no una llista de dades** (0.192.86): a `ui.list` of data takes no tap on a row —the list draws and the page turns,
-- and picking is the dial's or the app's—, i el que aquesta pàgina vol és **triar un caràcter amb el dit**; `ui.icon_grid` és el control
-- del sistema que fa exactament això: cel·les, la triada invertida, paginador, i `on_pick` amb el número sencer (docs/30, docs/90).
-- Cada cel·la és el caràcter i, a sota, el que en té el model: el número, o `ok` quan ja té les seves mostres.
local function build_the_model_view()
  title = keep(ui.label(wid - 2 * MARGIN, title_h))
  if title ~= nil then
    title.text = 'The model'
    title.scale = kTitleScale
    title.align = 'left'
  end
  local done, sum, all = taught_total()
  local capacity = (hand.caps() and hand.caps().capacity) or 0
  subtitle = keep(ui.label(wid - 2 * MARGIN, sub_h))
  if subtitle ~= nil then
    subtitle.text = string.format('%d/%d chars  %d/%d examples', done, all, sum, capacity)
    subtitle.scale = kFootScale
    subtitle.align = 'left'
    subtitle.color = 0
  end
  -- **La llista pren el que queda de la pàgina** (0.192.86): the room it gets is the page minus the two lines of above and the row of
  -- buttons under it, o sigui que la llista és el que mana i no un número de sortida (docs/30).
  row_h = ui.line_height(kFootScale + 1) + 18
  list_h = math.max(row_h * 3, hei - (2 * MARGIN + title_h + sub_h + GAP + GAP + 2 * ROW_H + GAP + foot_h))
  -- **Una llista de files, que és el que l'amo volia** (0.192.86: *«una llista per déu!»* i *«la graella es congestiona els ulls sense
  -- motiu»*): a `ui.list` de dades no li arriba el toc d'una fila —el nucli no té cap branca per a ella al despatx de l'activació—, i
  -- el que sí que el pren és una **fila que és un control** (`ui.list_entry`): la lletra a l'esquerra, les seves mostres a la dreta, la
  -- fila triada invertida i un toc meu (docs/30, docs/90).
  the_list = keep(ui.list(wid - 2 * MARGIN, list_h))
  if the_list ~= nil then
    the_list.row_height = row_h
    local list = the_characters()
    for i = 1, #list do
      local c = list:sub(i, i)
      local n = taught(c)
      local entry = keep(ui.list_entry(wid - 2 * MARGIN, row_h))
      if entry ~= nil then
        entry.marked = (i == work.index)
        local left = keep(ui.label(row_h * 2, row_h))
        if left ~= nil then
          left.text = c
          left.scale = 4
          left.align = 'left'
          left:place(14, 0)
          entry:add(left)
        end
        local right = keep(ui.label(wid - 2 * MARGIN - row_h * 2 - 28, row_h))
        if right ~= nil then
          -- **La tinta la mana la fila**: a label inside a marked entry is swapped by the system —white on ink—, o sigui que el
          -- número no ha de portar cap gris propi o es queda il·legible a la fila triada (docs/30, docs/92).
          right.text = string.format('%d of %d', n, kGoal)
          right.scale = kFootScale + 1
          right.align = 'right'
          right.color = 0
          right:place(row_h * 2 + 14, 0)
          entry:add(right)
        end
        entry.on_click = function()
          work.index = i
          save_work()
          needs_a_paint = true
        end
        the_list:add(entry)
      end
    end
  end
  -- **La cantonada de dalt a la dreta és la tornada, i amb la fletxa** (0.192.86, l'amo: *«perquè surt la roda dentada a dalt a la vista
  -- de settings?»*): the gear belongs to the page that opens this one; here it would say "settings" from inside settings, so the same
  -- corner carries the arrow back i no hi ha cap botó de més a baix. És una tecla de quaranta-vuit píxels i sense marc, com la roda de
  -- l'altra pàgina (docs/30, docs/90).
  gear = keep(ui.button(48, 44))
  if gear ~= nil then
    gear.icon = 'back'
    gear.framed = false
    gear.on_click = function()
      -- **Tornar de la vista del model refà la pàgina** (0.1.15, l'amo: *«El botó "back" "<" de Settings no sembla anar fi… o no es
      -- refresca la vista principal al sortir de la vista settings»*): `needs_a_paint` només repinta els controls que hi ha —els del
      -- model—, o sigui que canviar de vista necessita el camí sencer, que és un altre senyal i es fa al torn (docs/30, docs/90).
      mode = 'draw'
      needs_a_rebuild = true
    end
  end
  local c = the_character_at(work.index)
  forget_button = keep(ui.button(wid - 2 * MARGIN, ROW_H - 8))
  if forget_button ~= nil then
    forget_button.icon = 'close'
    forget_button.title = (c ~= nil) and ('Forget ' .. c) or 'Forget'
    forget_button.scale = 3
    forget_button.disabled = (c == nil) or (taught(c) == 0)
    forget_button.on_click = function()
      local now = the_character_at(work.index)
      if now == nil then return end
      local gone = hand.forget(work.zone, now)
      if footer ~= nil then
        footer.text = string.format('%d example(s) of %s forgotten', gone or 0, now)
      end
      needs_a_paint = true
    end
  end
  -- **I el de tota la zona, a sota** (0.192.86, l'amo: *«"Forget [symbol]" i assota un "Forget all"»*): the second one is the hand's
  -- whole memory of the zone —the page of a person who wants to start again—, i demana dos tocs perquè no es pren per error: la
  -- primera premuda el deixa «Press again», i la segona esborra (docs/30, docs/70).
  forget_all_button = keep(ui.button(wid - 2 * MARGIN, ROW_H - 8))
  if forget_all_button ~= nil then
    forget_all_button.icon = 'close'
    forget_all_button.title = 'Forget all'
    forget_all_button.scale = 3
    -- **El compte de la zona surt del model, i `hand.count` vol el caràcter** (0.1.16, l'amo: *«Perqué el botó "Forget all" està
    -- desactivat?»*): `hand.count(zone)` sense caràcter no és cap número —la porta demana el byte i respon `nil` amb una queixa—, o
    -- sigui que la comparació donava sempre zero i el botó no s'encenia mai, amb dues mostres al model i tot. El número que mana és
    -- el que ja compta la línia de dalt (docs/30).
    forget_all_button.disabled = (sum == 0)
    forget_all_button.on_click = function()
      if not forget_all_armed then
        forget_all_armed = true
        if forget_all_button ~= nil then forget_all_button.title = 'Press again' end
        if footer ~= nil then
          footer.text = string.format('this erases every example of %s: press again', work.zone)
        end
        ui.present()
        return
      end
      forget_all_armed = false
      hand.forget(work.zone)
      hand.save(work.zone)
      if footer ~= nil then footer.text = 'the ' .. work.zone .. ' model is empty' end
      needs_a_paint = true
    end
  end
  footer = keep(ui.label(wid - 2 * MARGIN, foot_h))
  if footer ~= nil then
    footer.scale = kFootScale
    footer.align = 'left'
    footer.color = 0
    footer.text = (c ~= nil) and ('the page draws ' .. c) or 'every character has its examples'
  end
end

local function build_the_view()
  local wide = wid > hei
  measure_the_text_boxes()
  if mode == 'model' then
    build_the_model_view()
    return
  end
  title = keep(ui.label(wid - 2 * MARGIN, title_h))
  if title ~= nil then
    title.text = 'Handwrite'
    title.scale = kTitleScale
    title.align = 'left'
  end
  subtitle = keep(ui.label(wid - 2 * MARGIN, sub_h))
  if subtitle ~= nil then
    subtitle.text = 'draw it on the paper'
    subtitle.scale = kSubScale
    subtitle.align = 'left'
    subtitle.color = 0
  end
  -- **La roda dentada, que porta al que el model té** (0.192.86, l'amo: *«usa una icona de roda dentada per anar a la vista on està
  -- el botó d'esborrar»*): una tecla de quaranta-vuit píxels a la dreta del títol, amb la marca del sistema i sense paraula (docs/30).
  gear = keep(ui.button(48, 44))
  if gear ~= nil then
    gear.icon = 'settings'
    gear.framed = false
    gear.on_click = function()
      mode = 'model'
      needs_a_rebuild = true
    end
  end
  -- **El caràcter gran, en un label i demanat des del torn** (0.192.79, l'amo: *«la lletra gran continua desapareixent»*): the label is
  -- the control that paints a character with the face of the page, i el que faltava no era el control sinó el moment —un hook corre
  -- entre dos torns i el dibuix que s'hi demana no arriba al vidre—, o sigui que el text es canvia al torn propi (docs/30, docs/50).
  -- **El caràcter gran, amb la cara del sistema**, que des de la 0.192.83 porta el joc Latin-1 i per tant les vuit lletres
  -- accentuades (docs/30). Abans la pàgina es carregava la seva pròpia cara; amb el sistema ja no cal, i són vint kilobytes que no
  -- es gasten.
  big = keep(ui.label(wid - 2 * MARGIN, char_h))
  if big ~= nil then
    big.text = '-'
    big.scale = kCharScale
    big.align = 'center'
    big.color = 0
  end
  hint = keep(ui.label(wid - 2 * MARGIN, hint_h))
  if hint ~= nil then
    hint.text = '-'
    hint.scale = kHintScale
    hint.align = 'center'
    hint.color = 0
  end
  -- **Els dos panells, amb l'espai que queda i no amb un número de sortida** (0.192.79, l'amo: *«el panell petit li sobra molt espai
  -- sota»*): the grid is seven cells of a side that is computed from the width, and its canvas is exactly that —no dead space under
  -- it— and the paper takes the rest of the page, so a bigger font or a smaller glass does not push anything off (docs/30).
  grid_side = math.min(30, (wid - 2 * MARGIN - 2 * kFramePx) // 7)
  grid_h = 7 * grid_side + 2 * kFramePx
  local used = 2 * MARGIN + title_h + sub_h + GAP + char_h + hint_h + GAP + GAP + grid_h + GAP + ROW_H + GAP + foot_h
  paper_max = math.max(160, hei - used)
  paper = keep(ui.canvas(wid - 2 * MARGIN, paper_max))
  grid_canvas = keep(ui.canvas(wid - 2 * MARGIN, grid_h))
  local fourth = (wid - 2 * MARGIN - 3 * GAP) // 4
  local button_h = ROW_H - 8
  retry = keep(ui.button(fourth, button_h))
  if retry ~= nil then
    retry.title = 'Retry'
    retry.scale = 3
    retry.on_click = function()
      strokes = {}
      erase_the_paper()
      last_grid = nil
      arm_the_paper()
      needs_a_paint = true
    end
  end
  skip = keep(ui.button(fourth, button_h))
  if skip ~= nil then
    skip.title = 'Skip'
    skip.scale = 3
    skip.on_click = function()
      strokes = {}
      erase_the_paper()
      last_grid = nil
      arm_the_paper()
      work.index = work.index + 1
      if the_character_at(work.index) == nil then work.index = 1 end
      save_work()
      needs_a_paint = true
    end
  end
  zone_button = keep(ui.button(fourth, button_h))
  if zone_button ~= nil then
    zone_button.title = (work.zone == 'letters') and '123' or 'abc'
    zone_button.scale = 3
    zone_button.on_click = function()
      work.zone = (work.zone == 'letters') and 'symbols' or 'letters'
      work.index = 1
      work.testing = false
      work.hits, work.tries = 0, 0
      strokes = {}
      erase_the_paper()
      last_grid = nil
      arm_the_paper()
      save_work()
      -- **Canviar de zona no refà la pàgina** (0.192.79): releasing eleven controls and making them again to change one letter is
      -- what made the big character stay off the glass —el label nou es quedava sense el seu tros de pàgina—; el que ha de canviar
      -- és el títol del botó, el caràcter, les dues superfícies i prou (docs/30, docs/85).
      if zone_button ~= nil then zone_button.title = (work.zone == 'letters') and '123' or 'abc' end
      needs_a_paint = true
    end
  end
  save_button = keep(ui.button(fourth, button_h))
  if save_button ~= nil then
    save_button.title = 'Save'
    save_button.primary = true
    save_button.scale = 3
    save_button.on_click = function()
      local ok = hand.save(work.zone)
      if ok then dirty = false end
      if footer ~= nil then
        footer.text = ok and 'the model is on the card' or 'the model would not be saved'
      end
      -- **`Save` i prou: si la lletra d'aquesta pàgina ja té les seves mostres, la pàgina passa a la primera que en falta** —i si no
      -- en falta cap, comença la passada de prova—, que és l'altre moment en què l'amo vol que el caràcter canviï (docs/30).
      local c = the_character_at(work.index)
      if c ~= nil and taught(c) >= kGoal then
        local next_one = the_first_missing()
        if next_one ~= nil then
          work.index = next_one
        else
          work.testing = true
          work.index = 1
        end
        save_work()
      end
      -- **I el paper es neteja** (0.1.18, l'amo: *«Ara al handwrite es veu la meva traçada però no l'esborren… al polsar "Save"
      -- o "Skip" o "Retry" esborra-la»*): the letter is on the card now —the drawing has done its work—, o sigui que la pàgina
      -- queda amb el paper i la quadrícula nets per a la següent, i els tres botons fan el mateix amb el que hi ha dibuixat
      -- (docs/30, docs/85).
      strokes = {}
      erase_the_paper()
      last_grid = nil
      arm_the_paper()
      needs_a_paint = true
    end
  end
  footer = keep(ui.label(wid - 2 * MARGIN, foot_h))
  if footer ~= nil then
    footer.scale = kFootScale
    footer.align = 'left'
    footer.color = 0
    footer.text = '-'
  end
  show_the_character()
end

-- **La pàgina sencera, sempre pel mateix camí** (0.192.79, l'amo: *«la lletra gran continua desapareixent … SOLUCIONA-HO JA»*): the
-- page was rebuilt in four places and one of them —the change of zone— forgot the `ui.clear(15)`, o sigui que el caràcter nou es
-- demanava sobre una pàgina que encara era la vella i el label no arribava mai al vidre: el que es veia era la paraula i prou. Amb
-- una sola funció, la pàgina es renta sempre i el caràcter gran es demana sempre (docs/30, docs/85).
rebuild_the_page = function()
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  -- **El paper és el que es captura, i es torna a armar amb la pàgina** (0.1.17): a page that is built again is a new box, and the
  -- ring of the old one is thrown away with it (docs/30, docs/50).
  arm_the_paper()
  if mode == 'draw' then
    show_the_character()
    paint_the_paper()
    paint_the_grid()
  end
  present_the_page()
end

local function build()
  local c = ui.caps()
  wid, hei = c.width, c.height
  rebuild_the_page()
end

-- **El que la pàgina fa cada torn**: the finger becomes points of the stroke being drawn, i quan s'aixeca i passen els 1,2 segons
-- la lletra es tanca i va al model (docs/30, docs/50).
local function one_turn()
  if needs_a_rebuild then
    needs_a_rebuild = false
    needs_a_paint = false
    rebuild_the_page()
    return
  end
  if mode ~= 'draw' then
    -- **La vista del model no dibuixa**: the finger and the paper are the other page's, i un toc aquí no ha de fer-hi res (docs/30).
    if needs_a_paint then
      needs_a_paint = false
      rebuild_the_page()
    end
    return
  end
  -- **Els dits, tal com els ha capturat el nucli** (0.1.17): every frame that landed on the paper since the last turn, in the
  -- order it happened, with the line it belongs to. A line that ends and another that begins are two strokes and not one path,
  -- i el nucli és qui sap quan el dit s'ha aixecat: el marc que una pàgina llegeix és sempre l'últim, o sigui que un contacte que
  -- comença i s'acaba entre dos torns era una línia que no s'havia vist mai (docs/30, docs/50).
  local points, _, finger = touch.captured()
  local now = sys.time_ms() or 0
  for i = 1, #points do
    local p = points[i]
    if p.line ~= the_line then
      the_line = p.line
      strokes[#strokes + 1] = {}
      -- **Una lletra nova és un paper net**: the one before is already the model's and the grid's, and pintar la de després a
      -- sobre seria una lletra que no és cap de les dues (docs/30, docs/85).
      if paper_is_dirty then
        erase_the_paper()
        paint_the_paper()
      end
    end
    local stroke = strokes[#strokes]
    if stroke ~= nil then
      stroke[#stroke + 1] = { p.x - paper_x, p.y - paper_y }
    end
  end
  -- **El rellotge compta des de l'últim dit aixecat del paper, i qualsevol dit el cancel·la** (0.192.80, l'amo: *«si l'usuari
  -- torna a baixar el dit abans d'1,2 t'esperes fins que l'alliberi 1,2 segons sencers des de l'últim cop que va aixecar l'últim
  -- dit (no el primer)»*): the countdown used to run from the last point **on the paper**, o sigui que un dit que tornava a
  -- baixar fora del paper —per canviar d'angle— no aturava res i la mostra sortia tallada a mig traç. El que diu si hi ha un dit
  -- és la captura i no una llista de contactes llegida a mà (docs/30, docs/50).
  if finger then
    finger_is_down = true
    rest_from = nil
  elseif finger_is_down then
    finger_is_down = false
    rest_from = now
  end
  -- **I la pausa que diu que la lletra s'ha acabat** (l'amo: *«Esperar 1.2 segon per assegurar que ha acabat»*): és aquí on es
  -- pinta el traç gruixut i on la lletra va al model, i no abans.
  if rest_from ~= nil and #strokes > 0 and (now - rest_from) >= kRestMs then
    rest_from = nil
    the_glyph_is_done()
  end
  -- **I la pàgina demanada, aquí**: el caràcter gran, el paper, la quadrícula i el vidre, en aquest ordre i dins del torn (docs/30).
  if needs_a_paint then
    needs_a_paint = false
    if mode == 'model' then
      -- **La vista del model es refereix tota ella a la pàgina del davant**: els comptes i el botó d'esborrar són del caràcter on
      -- s'és, o sigui que el que canvia és la vista sencera i no un tros (docs/30).
      rebuild_the_page()
    else
      show_the_character()
      paint_the_paper()
      paint_the_grid()
      present_the_page()
    end
  end
end

-- ------------------------------------------------------------------ l'app

load_state()
load_work()
-- **El model del disc, si n'hi ha**: the page reads what the keyboard would read, so the counters it shows are the keyboard's
-- (docs/30, docs/90).
hand.load(work.zone)
build()

function on_resume()
  rebuild_the_page()
end

function on_resize(w, h)
  wid, hei = w, h
  rebuild_the_page()
end

function on_pause()
  save_state()
  save_work()
end

function on_save()
  save_state()
  save_work()
end

function on_close()
  -- **La captura es deixa anar amb l'app**: a box of a page that is gone is a page that comes back to find the ring full of a
  -- drawing nobody made (docs/30, docs/50).
  touch.capture()
  save_state()
  save_work()
  -- **I el model es desa amb l'app**: el que la persona ha ensenyat ha de poder fer servir el teclat sense prémer res, i una
  -- escriptura per sessió i no per traç és el que aquesta casa fa amb tot el que s'aprèn (docs/30, docs/70).
  hand.save(work.zone)
end

while true do
  one_turn()
  -- **El panell mana, i el bucle no el fa esperar** (0.192.87, l'amo: *«El més important en aquell moment és llegir el més acurat
  -- possible i el major nombre de mostres possibles»*): the chip hands a frame over every ten milliseconds, i un torn és el que fa
  -- que el nucli llegeixi el vidre; amb el pas lent el començament d'un traç que dura vint mil·lisegons no existia. Ara el bucle
  -- pregunta al mateix ritme sempre que el paper s'està mirant, i el que espera el torn és el nucli i no el bucle (0.1.17,
  -- docs/30, docs/50).
  sys.delay(kStepMs):await()
end
