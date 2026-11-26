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
local kStepMs = 40
-- **La tinta no es demana a cada punt** (docs/50, docs/85): a e-ink una passada per cada punt d'un dit és un martelleig, o sigui
-- que la pàgina es pinta cada dos-cents mil·lisegons mentre es dibuixa i sempre en aixecar el dit.
local kInkEveryMs = 200

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
local TITLE_H = 52
local SUB_H = 34
local CHAR_H = 96
local PAPER_H = 280
local GRID_H = 132
local ROW_H = 58
local FOOT_H = 34

local paper_max = 300

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
  local layout, zone, index, testing = text:match('^(%S+) (%S+) (%d+) (%d+)$')
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
local drawing = false
local last_move = 0
local ink_at = 0
local ink_dirty = false
local last_grid = nil

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
end

-- **Els traços al paper**, amb la línia del canvas i no amb un rectangle per pas del dit: `cv:line` ja és la primitiva que
-- dibuixa un tros, i el que la pàgina ha de decidir és el gruix (aquí, tres píxels: un dit) (docs/30, docs/90).
local function paint_the_strokes()
  if paper == nil then return end
  paper:clear(15)
  -- **El paper s'ha de veure** (0.192.71): a white area on a white page is a page that does not say where the finger draws.
  local box = paper:caps()
  paper:frame(0, 0, box.width, box.height, 8)
  for _, stroke in ipairs(strokes) do
    if #stroke == 1 then
      paper:rect(stroke[1][1] - 1, stroke[1][2] - 1, 3, 3, 0)
    end
    for i = 2, #stroke do
      paper:line(stroke[i - 1][1], stroke[i - 1][2], stroke[i][1], stroke[i][2], 0)
      paper:line(stroke[i - 1][1], stroke[i - 1][2] + 1, stroke[i][1], stroke[i][2] + 1, 0)
    end
  end
  paper:invalidate()
end

-- **I la quadrícula que el model veu**, que és el que fa confiar-hi: seven rows of seven cells with three tones, the stroke ends
-- included in the two things the cells cannot say —the number of strokes and the shape of the box— (docs/30, docs/90).
local function paint_the_grid()
  if grid_canvas == nil then return end
  grid_canvas:clear(15)
  local box = grid_canvas:caps()
  grid_canvas:frame(0, 0, box.width, box.height, 8)
  if last_grid == nil then
    grid_canvas:invalidate()
    return
  end
  local cell = math.min(box.width, box.height) // 7
  if cell < 6 then cell = 6 end
  local side = cell * 7
  local x0 = (box.width - side) // 2
  local y0 = (box.height - side) // 2
  for y = 0, 6 do
    for x = 0, 6 do
      local level = tonumber(last_grid.cells:sub(y * 7 + x + 1, y * 7 + x + 1)) or 0
      local ink = (level == 0) and 15 or ((level == 1) and 8 or 0)
      grid_canvas:rect(x0 + x * cell, y0 + y * cell, cell - 1, cell - 1, ink)
    end
  end
  grid_canvas:invalidate()
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
    footer.text = string.format('%s: %d of %d · %d of %d letters ready', c, taught(c), kGoal, done, all)
  end
end

local function layout()
  local y = MARGIN
  if title ~= nil then
    title:resize(wid - 2 * MARGIN, TITLE_H)
    title:place(MARGIN, y)
  end
  y = y + TITLE_H
  if subtitle ~= nil then
    subtitle:resize(wid - 2 * MARGIN, SUB_H)
    subtitle:place(MARGIN, y)
  end
  y = y + SUB_H + GAP
  if big ~= nil then
    big:resize(wid - 2 * MARGIN, CHAR_H)
    big:place(MARGIN, y)
  end
  y = y + CHAR_H
  if hint ~= nil then
    hint:resize(wid - 2 * MARGIN, SUB_H)
    hint:place(MARGIN, y)
  end
  y = y + SUB_H + GAP
  if paper ~= nil then
    paper_x, paper_y = MARGIN, y
    paper:place(MARGIN, y)
  end
  y = y + paper_max + GAP
  if grid_canvas ~= nil then
    grid_x, grid_y = MARGIN, y
    grid_canvas:place(MARGIN, y)
  end
  y = y + GRID_H + GAP
  local third = (wid - 2 * MARGIN - 3 * GAP) // 4
  if retry ~= nil then retry:place(MARGIN, y) end
  if skip ~= nil then skip:place(MARGIN + third + GAP, y) end
  if zone_button ~= nil then zone_button:place(MARGIN + 2 * (third + GAP), y) end
  if save_button ~= nil then save_button:place(MARGIN + 3 * (third + GAP), y) end
  y = y + ROW_H + GAP
  if footer ~= nil then
    footer:resize(wid - 2 * MARGIN, FOOT_H)
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
end

local function the_glyph_is_done()
  if #strokes == 0 then return end
  local drawn = strokes
  strokes = {}
  local grid = hand.grid(drawn)
  if grid == nil then
    -- **Una premsa no és una lletra**: the drawing was a tap or had no shape, i el que fa la pàgina és dir-ho i esborrar-la.
    if footer ~= nil then footer.text = 'that was not a drawing: try again' end
    paint_the_strokes()
    return
  end
  last_grid = grid
  paint_the_grid()
  local c = the_character_at(work.index)
  if c ~= nil then
    if work.testing then
      local answer = hand.guess(work.zone, drawn)
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
      local added = hand.add(work.zone, c, drawn)
      if footer ~= nil then
        footer.text = added and ('learned ' .. c .. ' (' .. taught(c) .. ' of ' .. kGoal .. ')')
          or ('the same drawing of ' .. c .. ' was already there')
      end
      -- **Quan el caràcter ja té les seves mostres, la pàgina passa al següent**, i quan tots les tenen comença la prova.
      if taught(c) >= kGoal then
        local next_one = the_first_missing()
        work.index = next_one or work.index
        if next_one == nil then
          work.testing = true
          work.index = 1
        end
      end
    end
  end
  save_work()
  -- **La pàgina no es torna a fer per cada lletra**: the labels change and the paper is cleaned; rebuilding every control for
  -- every stroke would be a whole page of ink per letter (docs/30, docs/85).
  show_the_character()
  paint_the_strokes()
  paint_the_grid()
  ui.present()
end

-- ------------------------------------------------------------------ la vista

local function build_the_view()
  local wide = wid > hei
  title = keep(ui.label(wid - 2 * MARGIN, TITLE_H))
  if title ~= nil then
    title.text = 'Handwrite'
    title.scale = 5
    title.align = 'left'
  end
  subtitle = keep(ui.label(wid - 2 * MARGIN, SUB_H))
  if subtitle ~= nil then
    subtitle.text = 'draw it on the paper'
    subtitle.scale = 3
    subtitle.align = 'left'
    subtitle.color = 4
  end
  big = keep(ui.label(wid - 2 * MARGIN, CHAR_H))
  if big ~= nil then
    big.text = '-'
    big.scale = 10
    big.align = 'center'
    big.color = 0
  end
  hint = keep(ui.label(wid - 2 * MARGIN, SUB_H))
  if hint ~= nil then
    hint.text = '-'
    hint.scale = 4
    hint.align = 'center'
    hint.color = 4
  end
  paper_max = math.min(PAPER_H, math.max(160, hei // 3))
  paper = keep(ui.canvas(wid - 2 * MARGIN, paper_max))
  grid_canvas = keep(ui.canvas(wid - 2 * MARGIN, GRID_H))
  local fourth = (wid - 2 * MARGIN - 3 * GAP) // 4
  local button_h = ROW_H - 8
  retry = keep(ui.button(fourth, button_h))
  if retry ~= nil then
    retry.title = 'Retry'
    retry.scale = 2
    retry.on_click = function()
      strokes = {}
      last_grid = nil
      paint_the_strokes()
      paint_the_grid()
      ui.present()
    end
  end
  skip = keep(ui.button(fourth, button_h))
  if skip ~= nil then
    skip.title = 'Skip'
    skip.scale = 2
    skip.on_click = function()
      strokes = {}
      work.index = work.index + 1
      if the_character_at(work.index) == nil then work.index = 1 end
      save_work()
      show_the_character()
      ui.present()
    end
  end
  zone_button = keep(ui.button(fourth, button_h))
  if zone_button ~= nil then
    zone_button.title = (work.zone == 'letters') and '123' or 'abc'
    zone_button.scale = 2
    zone_button.on_click = function()
      work.zone = (work.zone == 'letters') and 'symbols' or 'letters'
      work.index = 1
      work.testing = false
      work.hits, work.tries = 0, 0
      strokes = {}
      last_grid = nil
      save_work()
      give_back_the_view()
      build_the_view()
      layout()
      show_the_character()
      paint_the_strokes()
      paint_the_grid()
      ui.present()
    end
  end
  save_button = keep(ui.button(fourth, button_h))
  if save_button ~= nil then
    save_button.title = 'Save'
    save_button.primary = true
    save_button.scale = 2
    save_button.on_click = function()
      local ok = hand.save(work.zone)
      if footer ~= nil then
        footer.text = ok and 'the model is on the card' or 'the model would not be saved'
      end
      ui.present()
    end
  end
  footer = keep(ui.label(wid - 2 * MARGIN, FOOT_H))
  if footer ~= nil then
    footer.scale = 3
    footer.align = 'left'
    footer.color = 0
    footer.text = '-'
  end
  show_the_character()
end

local function build()
  local c = ui.caps()
  wid, hei = c.width, c.height
  ui.clear(15)
  build_the_view()
  layout()
  paint_the_strokes()
  paint_the_grid()
  ui.present()
end

-- **El que la pàgina fa cada torn**: the finger becomes points of the stroke being drawn, i quan s'aixeca i passen els 1,2 segons
-- la lletra es tanca i va al model (docs/30, docs/50).
local function one_turn()
  local fingers = touch.read()
  local now = sys.time_ms() or 0
  local on_the_paper = false
  if #fingers > 0 then
    local finger = fingers[1]
    local x, y = finger.x - paper_x, finger.y - paper_y
    if x >= 0 and y >= 0 and paper ~= nil then
      on_the_paper = true
      if not drawing then
        drawing = true
        strokes[#strokes + 1] = {}
      end
      local stroke = strokes[#strokes]
      stroke[#stroke + 1] = { x, y }
      ink_dirty = true
      last_move = now
    end
  end
  if drawing and not on_the_paper then
    drawing = false
    last_move = now
  end
  if #fingers == 0 then drawing = false end
  -- **La tinta mentre el dit es mou**, però no a cada punt: dues-centes mil·lisegons és el que aquest vidre vol (docs/85).
  if ink_dirty and (now - ink_at) >= kInkEveryMs then
    paint_the_strokes()
    ink_at = now
    ink_dirty = false
    ui.present('gl16')
  end
  -- **I la pausa que diu que la lletra s'ha acabat** (l'amo: *«Esperar 1.2 segon per assegurar que ha acabat»*).
  if not drawing and #strokes > 0 and (now - last_move) >= kRestMs then
    the_glyph_is_done()
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
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  paint_the_strokes()
  paint_the_grid()
  ui.present()
end

function on_resize(w, h)
  wid, hei = w, h
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  paint_the_strokes()
  paint_the_grid()
  ui.present()
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
  save_state()
  save_work()
  -- **I el model es desa amb l'app**: el que la persona ha ensenyat ha de poder fer servir el teclat sense prémer res, i una
  -- escriptura per sessió i no per traç és el que aquesta casa fa amb tot el que s'aprèn (docs/30, docs/70).
  hand.save(work.zone)
end

while true do
  one_turn()
  sys.delay(kStepMs):await()
end
