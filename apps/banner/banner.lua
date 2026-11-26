-- banner: **the badge you wear**, on a panel of LEDs (port of the paper project's `Banner`, 1.3.5).
--
-- A settings page —the name, how fast it walks and a `Play` button— and, behind it, a DOT MATRIX drawn like the screen of an
-- LCD: the paper is white, every lit dot of the name is a small filled square and nothing else is painted. The dots walk
-- across the glass one column at a time, like the sign of a shop.
--
-- **THE LETTERS ARE DOTS AND NOT A FACE.** No font of the system draws a dot matrix, and asking the system for one would be
-- a call that only this app needs: the 5 x 7 shapes travel in this file, as the seven rows of dots of every character. A dot
-- is a filled square —`ui.rect`— and that is the whole drawing this app does (docs/30).
--
-- **THE CELL IS MEASURED FROM THE SHORT EDGE** (the owner, 20/10: *«Fes els quadrats mes grans! vull que el text ocupi quasi
-- tota l'alçada de la pantalla»*): the seven rows of a letter are almost the whole SHORT edge, so the cell —one square dot—
-- is taken from it every time the room changes, and the long edge is only cut into columns to know how many dots of the
-- marquee fit.
--
-- **ONLY THE LIT DOTS ARE PAINTED** (the owner, 18/10: *«recorda que només has d'il·luminar les zones amb lletra, la resta
-- blanc»*). A dot is ink on paper: the field of grey discs of the first version made every step come out of the sixteen-grey
-- table —a rectangle with grey 13 in it cannot be sent with a four-level mode— and the panel paid 450 ms of dithered
-- waveform for a name that is black on white. **The pass of a step is `du`**, two flat tones and 120 ms, which is the
-- cheapest thing this glass draws and what the black of the dots and the white of the paper are made of (docs/50).
--
-- **EL TEXT VA GIRAT I LA PANTALLA NO** (the owner, 14/10: *«ha de ser en horitzontal, no cal girar la pantalla, simplement
-- es gira el text»*): the marquee walks the LONG edge and the letters grow across the SHORT one, so the sign comes out as a
-- banner and the panel is never asked to turn —a turn of the panel is a pass of the panel, and a pass is a blink—. With the
-- glass upright the columns of the marquee run down it and the rows of a letter run across it, and the sign is read with
-- the board turned a quarter to the left.
--
-- **THE NAME IS DATA AND THE RING IS FORM.** The name and the pace are the badge, and they live in this app's own file
-- (`fs.data('banner.txt')`); whether the marquee was walking and which control had the ring are the shape of the page and
-- go to the `state` block, so a board that naps in the middle of the badge comes back to the badge (docs/30, docs/45).
--
-- **NO CONTROL TAKES THE CENTRE KEY OF THE MARQUEE.** The badge is a page of one gesture —a tap stops it— and it is read
-- from the gestures and not from a control: the `up` of the press that started the badge belongs to the `Play` button, so
-- the same tap cannot both start and stop it.

local caps = sys.caps()
if not caps.display or not caps.touch then
  console.print('this board needs a glass and a touch panel for this')
  return
end

local Default = 'Hello I am...'
local FileName = 'banner.txt'   -- the name and the pace, inside this app's own folder

-- **A lit dot is ink and nothing else is painted.** Two levels and no grey, which is what makes the pass of a step the
-- cheapest one this panel has (docs/50, docs/92).
local Lit = 0
local Paper = 15

-- The paces a person can pick, in MILLISECONDS PER STEP, slowest first: the slider goes left to right from slow to fast,
-- which is the way everybody reads a speed control. A step is one column of dots.
local Speeds = { 1200, 900, 650, 450, 300, 200, 120 }

-- **The letters take this much of the short edge**: 0.92 of it in seven rows is a cell of 70 px on a glass of 540 x 960, a
-- letter of 490 px and a lit square of 56, which is the number the owner asked for.
local Fill = 0.92

-- **The qualities this app names**: the page is sixteen levels and one pass (a text box and a slider want their greys), and
-- a step of the marquee is two flat tones.
local kPageMode = 'gl16'
local kStepMode = 'du'

-- **The sizes of the paper's face, in pixels of line** (docs/45): small 33 and body 43, asked for by the size and not by
-- the step, so the page keeps the same physical size as the other apps (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end
  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end
  return scale
end

local kSmall, kBody = scale_for(33), scale_for(43)

-- The keys of the page, measured in pixels: a text box, the slider with its line of milliseconds, the door and two lines
-- of help.
local kBoxH, kSliderH, kButtonH = 56, 96, 63
local Margin, Gap = 12, 8

-- ------------------------------------------------------------------ the shapes

-- A 5 x 7 dot matrix: one number per row, five bits, and **the bit 4 is the leftmost dot**. It is the classic geometry of a
-- sign, and it is small enough to read legibly when every dot is a square the size of the cell.
local Shapes = {
  [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
  ['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
  ['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
  ['C'] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
  ['D'] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
  ['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
  ['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
  ['G'] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
  ['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
  ['I'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
  ['J'] = {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
  ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
  ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
  ['M'] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
  ['N'] = {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11},
  ['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
  ['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
  ['Q'] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
  ['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
  ['S'] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
  ['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
  ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
  ['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
  ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11},
  ['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
  ['Y'] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
  ['Z'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
  ['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
  ['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
  ['2'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
  ['3'] = {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
  ['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
  ['5'] = {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
  ['6'] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
  ['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
  ['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
  ['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
  ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
  [','] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08},
  ['!'] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
  ['?'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
  ['-'] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
  [':'] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00},
  ['+'] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
  ['/'] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10},
  ["'"] = {0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00},
  ['*'] = {0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00},
}

-- **THE ACCENTS ARE FOLDED ONTO THEIR LETTER.** A dot matrix of 5 x 7 has no room for a mark above a letter, and a name in
-- Catalan has to be readable anyway: what goes on the panel is the letter without the mark.
local Accents = {
  ['à'] = 'A', ['á'] = 'A', ['ä'] = 'A', ['â'] = 'A',
  ['è'] = 'E', ['é'] = 'E', ['ë'] = 'E', ['ê'] = 'E',
  ['ì'] = 'I', ['í'] = 'I', ['ï'] = 'I', ['î'] = 'I',
  ['ò'] = 'O', ['ó'] = 'O', ['ö'] = 'O', ['ô'] = 'O',
  ['ù'] = 'U', ['ú'] = 'U', ['ü'] = 'U', ['û'] = 'U',
  ['ç'] = 'C', ['ñ'] = 'N', ['·'] = '.',
  ['À'] = 'A', ['Á'] = 'A', ['È'] = 'E', ['É'] = 'E', ['Ì'] = 'I',
  ['Í'] = 'I', ['Ò'] = 'O', ['Ó'] = 'O', ['Ù'] = 'U', ['Ú'] = 'U',
  ['Ç'] = 'C', ['Ñ'] = 'N',
}

-- ------------------------------------------------------------------ the state of the page (form)

local kStateBytes = 1024
local kLayout = 'v1'          -- bumped when a control is renamed or the page changes shape

local state = nil
local shape = { focus = nil }
local playing = false         -- the badge is on the glass and the page of settings is not

local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end

  return state
end

-- **What the page is now**: the control with the ring and whether the badge was walking. The name and the pace are the
-- badge and they are in `fs.data`, because a name is data and a ring is the shape of the page (docs/30, docs/45).
local function save_state()
  local block = the_state()
  if block == nil then return false end

  block:clear()
  block:write(0, kLayout .. '\nfocus=' .. (shape.focus or '') .. '\nplaying=' .. (playing and '1' or '0'))

  return true
end

-- **What the page was**: `nil` when there is nothing to read back or the block is of another shape, which is a page that
-- starts as a new one and not a ring on a control that no longer exists (docs/45).
local function load_state()
  local block = the_state()
  if block == nil then return nil end

  local body = block:read(0, kStateBytes) or ''
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end

  local now, first = {}, true

  for line in body:gmatch('[^\n]+') do
    if first then
      first = false
      if line ~= kLayout then return nil end
    else
      local key, value = line:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end

  if first then return nil end

  return now
end

-- ------------------------------------------------------------------ the badge (data)

local name = Default
local pace = 4                -- an index into Speeds, and not a number of milliseconds

local function pace_ms()
  return Speeds[pace]
end

local function save()
  local ground = fs.data()
  if ground ~= nil then pcall(fs.mkdir, ground) end

  local written, why = fs.write(fs.data(FileName), name .. '\n' .. tostring(pace) .. '\n')
  if written == nil then
    local said = (type(why) == 'table') and (why.code or why.message) or why
    console.print('[banner] could not save: ' .. tostring(said))
    return false
  end

  return true
end

-- **No save is not an error**: the first time the app is opened there is nothing to read, and a badge nobody has written
-- yet says what the note asked for. What is said is the reason, because a save that cannot be read is a name that will not
-- come back.
local function load()
  local body, why = fs.read(fs.data(FileName))
  if body == nil then
    console.print('[banner] no save (' .. tostring(type(why) == 'table' and (why.code or why.message) or why) ..
                  '): the default name')
    return false
  end

  local n = 0
  for piece in body:gmatch('[^\n]+') do
    n = n + 1
    if n == 1 then
      name = piece
    elseif n == 2 then
      local s = tonumber(piece) or pace
      if s < 1 then s = 1 end
      if s > #Speeds then s = #Speeds end
      pace = math.floor(s)
    end
  end

  if name == '' then name = Default end

  return true
end

-- ------------------------------------------------------------------ the space

local W, H = 0, 0
local alongIsVertical = false          -- the long edge of the glass is the vertical one
local Cell, DotGap, Dot, DotInset = 28, 8, 20, 4
local gridCols, gridRows, gridX, gridY, bandRow = 0, 0, 0, 0, 0

-- **The cell is the biggest square that lets seven rows fill the short edge**, and it is taken again every time the room
-- changes, which is what makes the same code a badge on the upright glass and on the flat one (docs/30).
local function measure(w, h)
  W, H = w, h

  local along = math.max(w, h)
  local across = math.min(w, h)
  alongIsVertical = (h >= w)

  Cell = math.floor((across * Fill) / 7)
  if Cell < 8 then Cell = 8 end

  DotGap = math.max(3, Cell // 5)
  Dot = Cell - DotGap
  DotInset = DotGap // 2

  gridCols = math.max(8, math.floor(along / Cell))
  gridRows = math.max(7, math.floor(across / Cell))

  -- The grid is centred, so both margins are the same and a turned glass does not leave it glued to one edge.
  gridX = math.floor((along - gridCols * Cell) / 2)
  gridY = math.floor((across - gridRows * Cell) / 2)

  bandRow = math.floor((gridRows - 7) / 2)
end

-- ------------------------------------------------------------------ the name

-- The name folded onto what the panel can draw: no accents, upper case, and anything the matrix does not know becomes a `?`.
local function fold(s)
  local out = {}
  local i, n = 1, #s

  while i <= n do
    local three = s:sub(i, i + 2)
    local two = s:sub(i, i + 1)

    if Accents[three] then
      out[#out + 1] = Accents[three]
      i = i + 3
    elseif Accents[two] then
      out[#out + 1] = Accents[two]
      i = i + 2
    else
      out[#out + 1] = Accents[s:sub(i, i)] or s:sub(i, i):upper()
      i = i + 1
    end
  end

  return table.concat(out)
end

-- **The shape of the name, column by column and as VERTICAL BITMASKS**, which is the question the painting asks: for this
-- column of dots, which rows are lit. It is built once when the badge starts and not on every step, because the name does
-- not change while it walks.
local function build_name()
  local cols = {}

  cols[#cols + 1] = 0            -- a blank column at each end, so the name does not touch the edge as it comes in and out

  local s = fold(name)
  for i = 1, #s do
    local rows = Shapes[s:sub(i, i)] or Shapes['?']

    for c = 0, 4 do
      local mask = 0
      for r = 1, 7 do
        if rows[r] & (1 << (4 - c)) ~= 0 then mask = mask | (1 << (7 - r)) end
      end
      cols[#cols + 1] = mask
    end

    cols[#cols + 1] = 0          -- the gap between two characters
  end

  cols[#cols + 1] = 0

  return cols
end

local letters = {}               -- the shape of the name while the badge walks
local nameDots = 0               -- how wide it is, in columns of dots
local scroll = 0                 -- how many columns the name has walked

-- ------------------------------------------------------------------ the paint

-- **A dot of the grid, TURNED WITH THE TEXT**: `c` walks the marquee —the LONG edge— and `r` is a row across the letters
-- —the SHORT one—, so with the glass upright the two swap places and the top of a letter ends against the right edge: the
-- sign is read with the board turned a quarter to the left.
local function dot_at(c, r)
  if alongIsVertical then
    return gridX + (gridRows - 1 - r) * Cell, gridY + c * Cell
  end

  return gridX + c * Cell, gridY + r * Cell
end

-- **The band of the letters, in pixels of the glass**: everything a step can change. A step slides the whole name one
-- column, so the band is the whole of what the badge paints —and nothing outside it is ever marked— (docs/30, docs/50).
local function band_box()
  local ax, ay = dot_at(0, bandRow)
  local bx, by = dot_at(gridCols - 1, bandRow + 6)

  return math.min(ax, bx), math.min(ay, by), math.abs(bx - ax) + Cell, math.abs(by - ay) + Cell
end

-- The whole band: paper and one filled square per LIT dot of the name, and **nothing is drawn where nothing is lit** —
-- which is what keeps the pass of a step at two tones (the owner, 18/10). `origin` is the grid column the first dot of the
-- name sits in, so a step of the marquee is one column of `scroll`.
local function paint_band()
  local bx, by, bw, bh = band_box()
  ui.rect(bx, by, bw, bh, Paper)

  local origin = gridCols - scroll
  local first = math.max(0, origin)
  local last = math.min(gridCols - 1, origin + nameDots - 1)

  for c = first, last do
    local mask = letters[c - origin + 1]
    if mask ~= nil and mask ~= 0 then
      for fy = 0, 6 do
        if (mask & (1 << (6 - fy))) ~= 0 then
          local x, y = dot_at(c, bandRow + fy)
          ui.rect(x + DotInset, y + DotInset, Dot, Dot, Lit)
        end
      end
    end
  end
end

-- **The page of settings is painted by the app and the controls win their own pixels**: the paper goes first and the
-- system draws the text box, the slider, the button and the two lines on top of it (docs/30).
local function paint_page()
  ui.clear(Paper, kPageMode)
  ui.present(kPageMode)
end

-- ------------------------------------------------------------------ the page (form)

local box, slider, pace_label, play, hint_a, hint_b

local function drop_page()
  for _, one in ipairs({ box, slider, pace_label, play, hint_a, hint_b }) do
    if one ~= nil then pcall(function() one:release() end) end
  end
  box, slider, pace_label, play, hint_a, hint_b = nil, nil, nil, nil, nil, nil
end

local function pace_line()
  return string.format('%d ms per step', pace_ms())
end

-- **The ring goes to a control that is there**: a name that is not one of them is not a ring, it is the name box, which is
-- where the page starts when nobody has touched it (docs/45).
local function put_the_ring()
  local wanted = { name = box, pace = slider, play = play }
  local one = wanted[shape.focus or ''] or box
  if one ~= nil then ui.focus(one) end
end

local start_badge
-- **El desig d'un control es fa al torn següent** (docs/30, docs/45): un botó que s'ha deixat anar es torna a pintar ell mateix
-- DESPRÉS del seu ganxo, i una pàgina que canvia el vidre dins del mateix gest es queda amb la paraula del botó vell
-- estampada on ja no és —el mateix `wanted` que fa servir el Clock—. La icona de `Play` diu «vols jugar» i el bucle ho fa.
local want_play = false

-- **The controls are made once and moved** (docs/30, docs/50): the keyboard coming up makes the view shorter and the app is
-- told so with a size it did not have, and a page that made its text box again on that turn would close the very keyboard
-- the person is typing with. So the box, the slider, the two lines and the door are built here and placed by `place_page`.
local function build_page()
  drop_page()

  box = ui.textbox(W - 2 * Margin, kBoxH)
  if box ~= nil then
    box.text = name
    box.cursor = #name
    box.scale = kBody
    box.on_change = function(self, text)
      name = text
      ui.present(kPageMode)
    end
  end

  slider = ui.slider(W - 2 * Margin, kSliderH)
  if slider ~= nil then
    slider.min = 1
    slider.max = #Speeds
    slider.step = 1
    slider.value = pace
    slider.on_change = function(self, value)
      local i = math.floor(tonumber(value) or pace)
      if i < 1 then i = 1 end
      if i > #Speeds then i = #Speeds end
      pace = i
      -- **El lliscador i la ratlla diuen el mateix**: the control shows a position and the line says what the position is
      -- worth, and a line that is not told is a line that lies (docs/30).
      if pace_label ~= nil then pace_label.text = pace_line() end
      save()
      paint_page()
    end
  end

  pace_label = ui.label(W - 2 * Margin, ui.line_height(kSmall))
  if pace_label ~= nil then
    pace_label.text = pace_line()
    pace_label.scale = kSmall
    pace_label.color = 4
    pace_label.background = Paper
  end

  play = ui.button(W - 2 * Margin, kButtonH)
  if play ~= nil then
    play.title = 'Play'
    play.scale = kBody
    play.on_click = function() want_play = true end
  end

  -- **Les dues ratlles de l'ajuda són del flux i no es pinten a mà** (the owner, 24/10: *«En mode apaisat es queda tallat
  -- l'UI, el text d'abaix es veu tallat per la pantalla»*): a label reserves the height of its line, so the last line of the
  -- page ends where the glass ends, turned or not (docs/30, docs/45).
  hint_a = ui.label(W - 2 * Margin, ui.line_height(kSmall))
  if hint_a ~= nil then
    hint_a.text = 'Write a name and pick a pace,'
    hint_a.scale = kSmall
    hint_a.color = 8
    hint_a.background = Paper
    hint_a.align = 'center'
  end

  hint_b = ui.label(W - 2 * Margin, ui.line_height(kSmall))
  if hint_b ~= nil then
    hint_b.text = 'then press Play'
    hint_b.scale = kSmall
    hint_b.color = 8
    hint_b.background = Paper
    hint_b.align = 'center'
  end
end

-- Where each control goes with the room the glass has now, which is the whole of what a turned page or a keyboard does to
-- this page: the heights are fixed, the width is the page's, and the two lines of help are the first to go when the room is
-- a keyboard short.
local function place_page()
  local w = W - 2 * Margin
  local y = Margin
  local line_h = ui.line_height(kSmall)

  if box ~= nil then
    box:resize(w, kBoxH)
    box:place(Margin, y)
  end
  y = y + kBoxH + Gap

  if slider ~= nil then
    slider:resize(w, kSliderH)
    slider:place(Margin, y)
  end
  y = y + kSliderH + Gap

  if pace_label ~= nil then
    pace_label:resize(w, line_h)
    pace_label:place(Margin, y)
  end
  y = y + line_h + Gap

  if play ~= nil then
    play:resize(w, kButtonH)
    play:place(Margin, y)
  end
  y = y + kButtonH + Gap * 2

  local room = (y + 2 * line_h) <= (H - Margin)

  for _, one in ipairs({ hint_a, hint_b }) do
    if one ~= nil then
      if room then
        one:resize(w, line_h)
        one:place(Margin, y)
        y = y + line_h
      else
        -- **A control fora del vidre és un control que no hi és**: a line of help that does not fit goes away instead of
        -- being placed under the edge (docs/30, docs/45).
        one:place(0, H + 200)
      end
    end
  end

  put_the_ring()
end

-- ------------------------------------------------------------------ the badge on the glass

-- **The badge takes the whole glass** (the original's T206): the bar of the system goes and the view is the page from row
-- zero, which is what makes this a panel and not a frame with chrome above it. **Qui mana és el sistema i pot dir que no**:
-- a full screen that is refused is only the badge with the bar still there, and nothing else changes (docs/30).
-- **I s'assigna i no es declara**: el `Play` d'aquesta pàgina és una closure feta abans, i un `local function` aquí seria un
-- segon local que l'ombreja —el botó cridava un `nil`, trobat a l'emulador tot just estrenar l'app—.
start_badge = function()
  if name == '' then name = Default end

  save()

  letters = build_name()
  nameDots = #letters
  -- **El nom ja hi és quan el badge arrenca**: the marquee starts with the first letters on the glass instead of walking in
  -- from an empty screen —six seconds of white after pressing `Play` is a badge nobody has pressed— and the passes after
  -- the first one still come in from the edge, which is the shop sign of the original (docs/45).
  scroll = math.max(0, gridCols - 2)
  playing = true
  shape.focus = 'play'
  save_state()

  -- **L'arbre marxa i no només s'amaga**: a control is drawn on top of the app's own paint, so a page of settings that is
  -- still there would cover the badge with a text box and a button (docs/30, docs/45).
  drop_page()

  ui.full_screen(true)
  measure(ui.caps().width, ui.caps().height)

  paint_band()
  ui.present(kStepMode)

  console.print(string.format('[banner] the badge walks: %d columns, %d ms per step, cell %d, view %dx%d',
    nameDots, pace_ms(), Cell, W, H))
end

local function stop_badge()
  playing = false
  save_state()

  -- **I la pantalla es torna pel seu nom**: allò que es va demanar es deixa, encara que el sistema ja ho hagi desfet tot sol
  -- (una app que peta, o les tres tocades del centre). Demanar dues vegades el mateix no és cap error; deixar-ho demanat, sí.
  ui.full_screen(false)
  measure(ui.caps().width, ui.caps().height)

  build_page()
  place_page()
  paint_page()

  console.print('[banner] the badge is put away')
end

-- ------------------------------------------------------------------ the start

load()
measure(ui.caps().width, ui.caps().height)

local was = load_state()
if was ~= nil then
  if was.focus ~= nil and was.focus ~= '' then shape.focus = was.focus end
  if was.playing == '1' then playing = true end
end

if playing then
  -- **El badge torna on era**: the state says the marquee was walking, so it walks again —the letters and the ink of the
  -- first step, and no page of settings under it— (docs/30, docs/45).
  start_badge()
else
  build_page()
  place_page()
  paint_page()
end

function on_pause() save(); save_state() end
function on_save() save(); save_state() end

function on_close()
  save()
  save_state()
  -- **I el vidre sencer es torna al comiat**: an app that ends with the bar taken away leaves a board nobody can use, and
  -- the system undoes it too —this is the same wish said by the app that asked— (docs/30).
  if playing then ui.full_screen(false) end
end

-- ------------------------------------------------------------------ the loop

local next_step = 0

while true do
  -- **El que el botó ha demanat es fa aquí i no al seu ganxo** (docs/30, docs/45), i **abans de preguntar la mida**: demanar
  -- la pantalla sencera canvia la vista, i una mida llegida abans es creu un canvi de mida que no ha passat i la desfà.
  if want_play then
    want_play = false
    start_badge()
  end

  local view = ui.caps()

  if view.width ~= W or view.height ~= H then
    -- **A turned glass, or the keyboard coming up, is the same page with other boxes** (docs/30): the controls are moved and
    -- not made again, so the text box that the person is typing in keeps the keyboard it opened.
    local was_playing = playing
    measure(view.width, view.height)
    if was_playing then
      paint_band()
      ui.present(kStepMode)
    else
      place_page()
      paint_page()
    end
  end

  if playing then
    local now = sys.time_ms()
    if now >= next_step then
      next_step = now + pace_ms()

      scroll = scroll + 1
      if scroll > gridCols + nameDots then scroll = 0 end

      paint_band()
      ui.present(kStepMode)
    end
  end

  -- **Els gestos es prenen tots i la cua es buida**: a gesture nobody takes is a queue that grows (docs/30). Un toc atura el
  -- rètol; el de la pàgina el llegeixen els controls, que són del sistema i guanyen els píxels que són seus.
  local gesture = touch.gesture()
  while gesture do
    if playing and (gesture.kind == 'tap' or gesture.kind == 'long') then
      stop_badge()
    end

    gesture = touch.gesture()
  end

  sys.delay(playing and 20 or 30):await()
end
