-- life: Conway's game of life, on electronic ink.
--
-- Ported from the paper project (`data/app/Life/pos_launch.lua`, version 1.5.2) with **the screen and the behaviour
-- whole** and the original's numbers (docs/45). A grid of thirty-pixel cells, two generations and a rule of three
-- lines; a pause, a button of a new world and a finger that paints the world one cell at a time.
--
-- **EVERY GENERATION IS A PASS OF THE PANEL, AND THAT IS THE CLOCK.** That is why the refresh time is given in
-- MILLISECONDS —the two the settings offer, 1200 and 700— and why the ceiling is the glass and not the processor: a
-- generation that paints nothing is a generation nobody sees.
--
-- **TWO BOARDS AND NOTHING ELSE.** The new one is computed over the old —it cannot be done in place, because a cell
-- that changes would affect its neighbours— and at the end the two swap roles. No new Lua table per generation: the
-- two tables are made once, in `measure`, and filled with 0 and 1.
--
-- **THE WORLD IS A TORUS**: whoever leaves on the right comes back in on the left. Shorter to write and more fun to
-- watch than an edge that kills.
--
-- **THE REFRESH IS THE WHOLE OF THIS APP'S BUSINESS, AND IT IS THE ONE THE PAPER WROTE DOWN** (`docs/45`, the old
-- `DisplayManager`): a generation marks **one box** —the rows it changed, plus the row above and below each, because
-- the line between two cells is drawn INSIDE the one above and the one on the left— and asks for **the cheapest pass
-- this panel has**, `du`: two levels, 120 ms, and no blink. Two things follow from that and both are measured:
--   - **a pass costs its ink —120 ms, whatever its area— and then about a millisecond for every row of it**, so the
--     trade is between one box with the air of the rows between the changes in it and one box per group of changes with
--     a pass each. Two boxes is the ceiling here (`kMaxBoxes`), because the line of the counters is a third and a page
--     of four boxes is a page painted whole;
--   - **and the box must not be the whole glass**, because the kernel turns a `du` of seven eighths of the panel into
--     a `gc16` —the wash, 450 ms and two blinks— and that rule is the panel's, not this app's (docs/50). That is what
--     the strip of paper around the board is for: with the board smaller than the view, the strip of the counters and
--     the band of the world never touch, and neither of them is ever the glass.
--
-- **`du` PAINTS AND DOES NOT CLEAN** (measured on the board, docs/95 E-7): a cell that dies goes white with the ghost
-- of what it was, and the ghost adds up. So every `kWashEvery` generations the whole page is washed again —sixteen
-- levels, 450 ms and no blink, `gld16`—, which is the clean the paper's own policy asked for and the only one this
-- project allows: **nothing that blinks reaches this glass** (docs/92).
--
-- **A SOUP IS THE EXCEPTION AND IT IS THE PHYSICS OF THE PANEL**: while almost every row changes, the band is the whole
-- board and what costs is the rows themselves —about a millisecond each on this driver—, and no mode of the controller
-- makes that shorter. Measured on this emulator with the numbers the board gave: **1 415 ms a generation of a soup**
-- (of which 1 080 are the rows of the board) against the **1 770 ms** of the wash of the whole glass this page used to
-- ask for, and **about 500 ms** once the world moves in one place —a glider— where the box is five rows and the line of
-- the counters.
--
-- **THE SQUARES OF A SHAPE ARE SEEN.** Two cells that touch share their edge, so without a line a shape is one black
-- block. What shows them is a ONE-pixel line of paper between two cells that are BOTH alive —and only then—, drawn
-- after the fill. Erasing a pixel next to a dead cell would leave a row of paper that can never change, and the rows
-- that can change are what buys the clean pass of a new map (the original's T99).
--
-- **NO CONTROL TREE AND EVERY BUTTON PAINTED BY HAND.** The page has no focusable control, so the dial of the board is
-- the app's: the centre pauses, the two sides change the refresh time, and none of the three costs a control. The two
-- buttons of the strip are **a frame with a mark of the clipart in it** —the `random` of FontAwesome (U+F074), which is
-- what the world does when it is thrown away and made again, and the `cog` of the settings— and the same rectangles both
-- paint them and take the finger. The marks are a face of the app's own folder (`res/marks.ttf`, made by
-- `tools/make_keys.py` out of the same font as the keys of the time clock) and the words are what the buttons say when
-- the face is not on the disk, the fallback the other apps have (docs/45). The centre key is also the system's way out
-- —three presses inside a second and a half close the app— and a hook cannot take it away.
--
-- **AND A PAGE OF SETTINGS, WHICH IS THE OTHER THING A PAGE OF THIS APP CAN BE** (`setting`): the gear of the strip opens
-- it, the arrow that takes its place goes back, and the world stays stopped underneath while a person chooses. The page
-- offers the two things this app has that are a choice and not the game: **whether the page is washed every six
-- generations** and **which of the two refresh times it runs at**. A tap paints only the box it changed, in the cheapest
-- pass, because a page of settings is paper that does not move (docs/50).
--
-- **WHAT IS REMEMBERED AND WHAT IS NOT.** The world and the pause are the game, and they are thrown away when the app
-- closes, as the paper did. The two settings are a **preference** —not the shape of a view— so they live in the app's own
-- file (`/data/life/settings.txt`, `fs.data`) and the page comes back as the person left it, which is what docs/30 says of
-- the things that go in `fs.data` and not in a `state` session (docs/30, docs/45 §5 bis).

local caps = sys.caps()
if not caps.display then
  console.print('this board needs a glass for this')
  return
end

-- **Els tres tocs del vidre són d'aquest joc i no del sistema** (0.190.254, l'amo: *«Desactiva els 3 tocs sobre el Tetris, l'Snake
-- i qualsevol joc que no el tingui desactivat»*). Un joc llegeix cada toc, o sigui que el tercer no pot ser una ordre del sistema
-- que tanqui la partida al mig; el gest queda a la cua i aquest bucle el deixa caure, perquè només mira `tap` i `drag`. **I tornar
-- enrere no es perd**: les tres premsades de la tecla del mig són un altre camí i no les toca ningú, i quan l'app s'acaba el
-- sistema recupera els tres tocs sol (docs/30, docs/46).
if (touch.triple_tap_close ~= nil) then touch.triple_tap_close(false) end

local Cell = 30                        -- the size a cell aims for, in pixels
local Speeds = { 1200, 700 }           -- how long a generation lasts, in ms: the two the settings offer
local Top = 72                         -- the strip at the top: the counters and the two buttons
local Inset = 8                        -- the paper the board keeps around it, and the reason it is there is in
                                       -- the header: the strip and the band of a generation never touch, so neither
                                       -- box is ever the glass (docs/50)
local Btn, BtnH = 84, 63               -- a button of the strip: 63 is the touch minimum and 84 is what a mark of the
                                       -- clipart needs, because a title is centred with sixteen pixels of air each side
local BtnGap = 8                       -- the air between the two buttons and against the edge of the view

-- **The qualities this page asks for.** `du` is the fastest paint of this panel —two levels, 120 ms, no blink— and it is
-- what a generation asks for; `gld16` is the house's clean —sixteen levels, 450 ms, no blink— and it is what a new world
-- and the wash of every `kWashEvery` generations ask for (docs/50, docs/95).
--
-- **And a CHANGE OF PAGE is the one thing that needs the clean that blinks** (0.189.48, l'amo: *«al canviar a settings
-- hauràs d'usar alguna cosa que netegi molt bé, ara queda brut»*): a page of paper over a board of black cells leaves a
-- `gld16` at two sixteenths —level thirteen where a cell was, which is a bruise the person sees— and the only thing that
-- takes it is a waveform that drives the glass to black and white and back. `gl16` is the gentlest of those: sixteen
-- levels and **one** blink, against the two of `gc16`, which is the wash the system itself uses for its own card
-- (docs/50, docs/92). And a tap of the settings page is a small box that has to land clean too, so it takes `gld16` and
-- not the cheap pass: it is paper that does not move and nobody is watching its clock.
local kGenMode = 'du'
local kPageMode = 'gld16'
local kSettMode = 'gl16'
local kTapMode = 'gld16'

-- **How many generations a `du` is worth before the page is washed**: `du` leaves five sixteenths of what was there and
-- adds a sixteenth of dirt to it, so a board painted this way for ever ends grey — and the third of the quick passes
-- after the sixth is already a dark one. Six is the number this glass settles on: the wash costs 450 ms of ink whatever
-- its area, so it is the ink of four quick passes, and what is bought is a world that never looks like a bruise.
local kWashEvery = 6

-- **The size of the paper's face, in pixels of line** (docs/45): small is 33. Asking for the size and not the step
-- keeps the page the same physical size on both projects (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  return math.max(1, math.min(16, math.floor(line_px / step + 0.5)))
end

local kSmall = scale_for(33)

-- **THE MARK OF A NEW WORLD, AND THE WORD ONLY WHEN THE FACE IS NOT THERE**: `res/marks.ttf` is one shape answering the
-- code 1 —a text here is one byte a character, so the room no text of a page means is the codes below space— and it is
-- the road of the time clock (docs/30, docs/45). Five steps of an em of nine are 45 px, which is the 41 of the original
-- in a key of 84.
local marks, marks_why = ui.load_font('/res/marks.ttf')
if marks == nil then
  console.print('life: /res/marks.ttf: ' .. tostring(marks_why) .. ': the button says the word')
end
local kWorldMark = '\1'
local kMarkScale = 5

-- **THE TWO PREFERENCES OF THIS APP, AND WHERE THEY LIVE** (docs/30, docs/45): what the settings page offers is a
-- **preference** —the wash and the refresh time— and a preference is not the shape of a view, so it goes to the app's own
-- file and not to the `state` session, which is what the 2048 does with its game (docs/30, «el que no hi va»).
local kSettingsFile = 'settings.txt'
local washOn = true                    -- whether the page is washed every `kWashEvery` generations
local setting = false                  -- true while the page of the settings is what the glass shows
local wasPaused = false                -- what the board was before the settings took the glass

local GridX, GridY                     -- where the board starts, in pixels of the view: the inset is the paper
local GridW, GridH                     -- ...around it, and the box of the world is never the glass because of it
local PageW, PageH                     -- the view, which is where the strip is laid out and what a page covers
local StripW                           -- the room the counters have: the strip less the two buttons
local Cols, Rows                       -- how many cells fit in the board
local alive, fresh                     -- the two boards (0 and 1)
local gen, live = 0, 0                 -- counters
local paused = false
local due = 0                          -- when the next generation is due
local speed = 2                        -- an index into `Speeds`: the refresh time the person chose in the settings
local whole = true                     -- true when what is on the glass is a brand new board
local washes = 0                       -- generations painted with `du` since the last wash of the page

-- **The rows a generation changed**, which is what travels to the panel. The table is REUSED: a fresh list per
-- generation would fill the memory with rubbish.
local dirtyRows = {}
local nDirty = 0
local statusDirty = true               -- the strip of the top shows counters and the speed

-- ------------------------------------------------------------------------- the grid

-- The band that slot `i` of `n` gets when `size` pixels are shared. Exact SHARED edges: no gap and no overlap
-- between neighbours, because a line of paper between two rows is a row that can never change.
local function bound(i, n, size) return math.floor(i * size / n) end

-- A board is a FLAT list of 0 and 1, indexed `(row - 1) * Cols + col`.
local function index(row, col) return (row - 1) * Cols + col end

-- The cell with the turn of the world done: there are no edges here.
local function cell(row, col)
  row = (row - 1) % Rows + 1
  col = (col - 1) % Cols + 1
  return alive[index(row, col)]
end

-- A NEW MAP: the button's job, and what happens when the board dies out of boredom. It fills the WHOLE grid.
local function newMap()
  math.randomseed(sys.time_ms())
  gen, live = 0, 0

  for i = 1, Cols * Rows do
    local v = (math.random(100) <= 28) and 1 or 0
    alive[i] = v
    live = live + v
  end

  whole = true
end

-- The cells that fit in the board. The strip keeps what it needs, the paper keeps its inset, and the board takes the
-- REST — down to the last row and not one row more (the paper's 27/09).
local function fits(w, h)
  return math.max(4, math.floor(w / Cell + 0.5)),
    math.max(4, math.floor(h / Cell + 0.5))
end

-- THE BOARD, MEASURED FROM THE SPACE THAT IS THERE, and what was there is MOVED and not thrown away: a turn of the
-- glass must not kill the game (the original's `measure`).
local function measure(w, h)
  local oldBoard, oldCols, oldRows = alive, Cols, Rows

  GridX, GridY = Inset, Top + Inset
  GridW, GridH = w - 2 * Inset, h - GridY - Inset
  StripW = w - Inset - 2 * Btn - 2 * BtnGap
  Cols, Rows = fits(GridW, GridH)

  alive, fresh = {}, {}
  for i = 1, Cols * Rows do alive[i], fresh[i] = 0, 0 end

  if oldBoard then
    local fc = math.min(Cols, oldCols or 0)
    local fr = math.min(Rows, oldRows or 0)
    for r = 1, fr do
      for c = 1, fc do
        if oldBoard[(r - 1) * oldCols + c] == 1 then alive[index(r, c)] = 1 end
      end
    end
  end

  live = 0
  for i = 1, Cols * Rows do live = live + alive[i] end

  whole = true
end

-- The next generation, and —on the way— WHICH ROWS changed. A dead cell that is not repainted stays black on the
-- glass, so the ones that die count as much as the ones that are born.
local function step()
  nDirty = 0

  if live == 0 then newMap(); return end

  local remains = 0
  local k = 0

  for row = 1, Rows do
    local any = false

    for col = 1, Cols do
      local neighbours = cell(row - 1, col - 1) + cell(row - 1, col) + cell(row - 1, col + 1)
        + cell(row, col - 1) + cell(row, col + 1)
        + cell(row + 1, col - 1) + cell(row + 1, col) + cell(row + 1, col + 1)

      k = k + 1
      local v = alive[k]

      if v == 1 then
        v = (neighbours == 2 or neighbours == 3) and 1 or 0
      else
        v = (neighbours == 3) and 1 or 0
      end

      fresh[k] = v
      remains = remains + v
      if v ~= alive[k] then any = true end
    end

    if any then
      nDirty = nDirty + 1
      dirtyRows[nDirty] = row
    end
  end

  alive, fresh = fresh, alive
  live = remains
  gen = gen + 1
end

-- ------------------------------------------------------------------- drawing

-- Where a cell is, in pixels of the view.
local function cellBox(row, col)
  local x0 = GridX + bound(col - 1, Cols, GridW)
  local x1 = GridX + bound(col, Cols, GridW)
  local y0 = GridY + bound(row - 1, Rows, GridH)
  local y1 = GridY + bound(row, Rows, GridH)
  return x0, y0, x1, y1
end

-- **ONE CELL, AND THE LINES THAT ARE ITS**: the fill, and the paper line that separates it from the cell on its
-- right and from the one below —both drawn inside this cell's own box— when the two are alive (the original's T99).
-- The line against the left cell and the one above are painted by that cell, so every drawing of a cell is
-- accompanied by its neighbours in `paintAround` and `paintBand` and no line is left behind.
local function paintCell(row, col)
  local x0, y0, x1, y1 = cellBox(row, col)
  local w, h = x1 - x0, y1 - y0
  local k = index(row, col)

  ui.rect(x0, y0, w, h, alive[k] == 1 and 0 or 15)

  if alive[k] == 1 then
    if col < Cols and alive[k + 1] == 1 then ui.rect(x1 - 1, y0, 1, h, 15) end
    if row < Rows and alive[k + Cols] == 1 then ui.rect(x0, y1 - 1, w, 1, 15) end
  end
end

-- A cell and the eight around it: a finger paints one cell, and its neighbours are what draw the lines that touch it.
local function paintAround(row, col)
  for r = math.max(1, row - 1), math.min(Rows, row + 1) do
    for c = math.max(1, col - 1), math.min(Cols, col + 1) do paintCell(r, c) end
  end
end

-- **THE BOXES OF A GENERATION, AND HOW MANY.** A pass of this panel costs its ink —120 ms, whatever its area— plus about
-- a millisecond a row of wire, so the two ends of the trade are: one box with the air of the rows between the changes in
-- it, or one box per group of changes with a pass each. A soup changes every row and the answer is one box; a world that
-- moves a glider at the top and an oscillator at the bottom is two small boxes and 250 ms instead of 1 200. **Two boxes
-- is the ceiling here** and the reason is the third one: the line of the counters is a box of its own, and from the
-- fourth on the kernel paints the page whole —`present(mode)` joins what touches and paints what is left box by box—
-- (docs/30, docs/50).
local kMaxBoxes = 2

-- The rows a generation has to paint, as runs: each changed row with the one above and the one below —the line between
-- two rows is drawn inside the one above— joined when they touch, and the closest two runs joined until there are
-- `kMaxBoxes` at most. The two tables are reused, like the list of the rows.
local boxLo, boxHi = {}, {}
local nBoxes = 0

local function boxesOfChange()
  nBoxes = 0
  if nDirty == 0 then return end

  for i = 1, nDirty do
    local row = dirtyRows[i]
    local lo = math.max(1, row - 1)
    local hi = math.min(Rows, row + 1)
    if nBoxes > 0 and lo <= boxHi[nBoxes] + 1 then
      if hi > boxHi[nBoxes] then boxHi[nBoxes] = hi end
    else
      nBoxes = nBoxes + 1
      boxLo[nBoxes], boxHi[nBoxes] = lo, hi
    end
  end

  while nBoxes > kMaxBoxes do
    local at, gap = 1, boxLo[2] - boxHi[1]
    for i = 2, nBoxes - 1 do
      local d = boxLo[i + 1] - boxHi[i]
      if d < gap then at, gap = i, d end
    end
    boxHi[at] = boxHi[at + 1]
    for i = at + 1, nBoxes - 1 do boxLo[i], boxHi[i] = boxLo[i + 1], boxHi[i + 1] end
    nBoxes = nBoxes - 1
  end
end

-- And the cells of each of those runs, painted whole: the box is `nBoxes` rectangles wide and the air between two of
-- them is paper nobody paints.
local function paintBoxes()
  for i = 1, nBoxes do
    for row = boxLo[i], boxHi[i] do
      for col = 1, Cols do paintCell(row, col) end
    end
  end
end

-- The whole world, corner to corner.
local function paintGrid()
  for row = 1, Rows do
    for col = 1, Cols do paintCell(row, col) end
  end
end

-- **TWO SHORT LINES AND NOT ONE LONG ONE** (measured on the emulator, where the face of the flash is the widest this page
-- ever gets): `gen 16   86 alive` is seventeen characters and the strip has room for twenty, so the time of the refresh
-- goes on a line of its own —it changes when a key is pressed and not with every generation— (docs/50).
-- **Les dues pàgines i els seus controls**, said before they are built because a button of the board opens the page of
-- the settings and the button of that page comes back (0.189.48).
local board_ui = nil                   -- the two buttons of the board, made when the board is what the glass shows
-- **And they are announced here and given below**, because a hook runs later and a hook that closes over a `local function`
-- declared further down closes over a **global** that is nil: the two buttons of the strip were pressed and nothing
-- happened (found by the owner, 15/11).
-- ============================================================== the shape of the view (`state`, docs/30)

-- **LA PÀGINA DELS AJUSTOS I EL CERCLE TORNEN COM ELS VA DEIXAR LA PERSONA** (0.189.52, la norma de la sessió `state` de
-- docs/30 i docs/45 §5 bis): `page` diu si el vidre era el tauler o la pàgina dels ajustos i `focus` quin dels tres controls
-- del sistema tenia el cercle, pel nom del control. **Les dues preferències —el rentat i la velocitat— no hi són**: són
-- decisions de la persona i viuen a `/data/life/settings.txt` des de la 0.189.49 (docs/30), i el món tampoc, que és la partida.
--
-- **El codi és el mateix de totes les apps i es copia sencer** (docs/30): a format que només llegeix aquest fitxer és un
-- format que no llegeix ningú.
local kStateBytes = 1024
local kLayout = 'v1'       -- bumped when a control is renamed or the page changes shape
local state_block = nil    -- the block: asked once and kept for the life of the app

local function the_state()
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function escape(text)
  return (tostring(text or ''):gsub('\\', '\\\\'):gsub('\n', '\\n'))
end

local function unescape(text)
  return (text:gsub('\\(.)', function(c) return (c == 'n') and '\n' or c end))
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'focus', 'page', 'tab', 'scroll', 'marked' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
  end
  for name, value in pairs(now.check or {}) do
    lines[#lines + 1] = 'check:' .. name .. '=' .. (value and '1' or '0')
  end
  for name, value in pairs(now.text or {}) do
    lines[#lines + 1] = 'text:' .. name .. '=' .. escape(value)
  end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  -- **El bloc és de mil vint-i-quatre bytes i el que s'hi escriu és més curt**: el que hi ha darrere és el zero amb què el
  -- nucli va fer el bloc, i una línia amb zeros al darrere no és cap valor —`tonumber` en treu `nil` i un nom deixa de ser
  -- el nom—, o sigui que el text s'acaba a la primera marca de zero (docs/30).
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end
  local now = { check = {}, text = {} }
  local first = true
  for line in body:gmatch('[^\n]+') do
    if first then
      first = false
      if line ~= kLayout then return nil end
    elseif line:sub(1, 6) == 'check:' then
      local name, value = line:match('^check:([^=]+)=(%d)$')
      if name ~= nil then now.check[name] = (value == '1') end
    elseif line:sub(1, 5) == 'text:' then
      local name, value = line:match('^text:([^=]+)=(.*)$')
      if name ~= nil then now.text[name] = unescape(value) end
    else
      local key, value = line:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end
  if first then return nil end
  return now
end

-- **La forma**: `page` 1 és el tauler i 2 la pàgina dels ajustos, i `focus` el nom del control amb el cercle.
local shape = { page = 1, focus = nil }

local function remember_the_ring(on, name)
  if not on then return end
  shape.focus = name
  save_state(shape)
end

local openSettings, closeSettings, releaseBoard, buildBoard, newWorld

local function statusText()
  return string.format('gen %d   %d alive', gen, live)
end

local function speedText()
  return paused and 'paused' or (Speeds[speed] .. ' ms')
end

-- The height of a line at a scale, which is what centring a word in a box needs (docs/30).
local function line(scale) return scale * ui.line_height(1) end

-- **THE COUNTERS ARE A LINE OF TEXT AND NOTHING ELSE**: they change with every generation, so they are a pass of their
-- own, and the box is the line —not the strip— because the rows between here and the board would be air on the glass
-- every time. It is papered first, because a counter that grows is a shorter number over a longer one.
local function paintCounters()
  ui.rect(0, 0, StripW, 36, 15)
  ui.text(12, 2, statusText(), kSmall, 0)
end

-- **Whether it is running and how fast**, on the line under the counters: it is painted with a page and when a key or the
-- settings change it, which is a pass of half a strip and not one per generation.
local function paintSpeed()
  ui.rect(0, 36, StripW, 36, 15)
  ui.text(12, 38, speedText(), kSmall, 0)
end

-- **THE TWO BUTTONS LIVE IN THE STRIP**, at its right end: the world and the settings. They are **controls of the
-- system** and not frames painted by hand (0.189.48, l'amo: *«Et recordo que has d'usar els controls del sistema»*): a
-- `ui.button` draws its frame, its shadow, the focus and the press, the ring of the dial lands on it and the finger is
-- its own, which is the same road the page of the time zone took in 0.174.0 (docs/30, docs/92). The gear is the
-- **`settings` mark of the kernel** and the world is the face of the app (`res/marks.ttf`, the `random` of the clipart),
-- which is what a button of this board does when the mark it wants is not one of the six the kernel draws.
local function worldRect() return PageW - Inset - Btn, 4, Btn, BtnH end
local function gearRect() return PageW - Inset - 2 * Btn - BtnGap, 4, Btn, BtnH end

-- The two buttons of the board, made once and asked to draw after a page that went over them.
local function paintButtons()
  if board_ui == nil then return end
  board_ui.gear:invalidate()
  board_ui.world:invalidate()
end

-- The page: the whole view in one pass of sixteen levels that does not blink, when the app opens, when the view is
-- another one, when the world is new, and every `kWashEvery` generations —because `du` paints and does not clean, and
-- the wash is what takes the ghost of the ten quick passes away (docs/50, docs/95).
local function paintPage(mode)
  -- **The page is ONE box and not the sum of the ones in it**: papering the whole view marks it, everything drawn
  -- after falls inside that mark, and the wash is one pass of 450 ms instead of one for the board and another for the
  -- strip. `ui.clear` does not do this —a page the system already gave white is not a change— so the rect is said here
  -- (docs/30, docs/50).
  ui.rect(0, 0, PageW, PageH, 15)
  paintGrid()
  paintCounters()
  paintSpeed()
  paintButtons()
  ui.present(mode or kPageMode)
  whole = false
  statusDirty = false
  washes = 0
end

-- What a generation costs: **one box** for the world (the band of the rows it moved) and one for the counters, in the
-- cheapest pass of the panel. The wash is decided before the band is painted, so a generation that owes it is one page
-- and not a page on top of a band.
local function refresh()
  if whole or washes >= kWashEvery then
    paintPage()
    return
  end

  if setting then return end

  boxesOfChange()
  paintBoxes()
  paintCounters()
  ui.present(kGenMode)
  statusDirty = false
  washes = washes + 1
end

-- **THE TWO BUTTONS OF THE BOARD ARE CONTROLS OF THE SYSTEM** (0.189.48): a `ui.button` with the `settings` mark of the
-- kernel for the page of the settings, and one with the face of the app for a new world. They are made when the board is
-- what the glass shows and given back when the page of the settings takes it, because a control is drawn over the page
-- and a button of the board under a page of settings would be a button of nothing (docs/30).
buildBoard = function()
  if board_ui ~= nil then return true end

  local why
  local gear, world
  gear, why = ui.button(2, 2)
  if gear == nil then
    console.print('life: no button of the settings (' .. tostring(why or 'the memory') .. ')')
    return false
  end
  gear.icon = 'settings'
  gear.on_click = function() openSettings() end

  world, why = ui.button(2, 2)
  if world == nil then
    gear:release()
    console.print('life: no button of the world (' .. tostring(why or 'the memory') .. ')')
    return false
  end
  -- **The mark of the world is the app's**, because the kernel draws six marks and a shuffle is not one of them: the face
  -- is `res/marks.ttf` and the title is the code it answers to (docs/45). Without the face the button says the word, and a
  -- word that does not fit is cut by the button itself, which is what the frame is for.
  if marks ~= nil then
    world.title = kWorldMark
    world:font(marks)
    world.scale = kMarkScale
    -- **The ink of a mark of this system is the level four** (docs/40): the six marks the kernel draws and the five of the
    -- panel are drawn there, so the one of the app does the same and the strip has one ink and not two.
    world.color = 4
  else
    world.title = 'New'
    world.scale = kSmall
  end
  world.on_click = function() newWorld() end

  board_ui = { gear = gear, world = world }
  local gx, gy = gearRect()
  gear:resize(Btn, BtnH)
  gear:place(gx, gy)
  local wx, wy = worldRect()
  world:resize(Btn, BtnH)
  world:place(wx, wy)
  return true
end

releaseBoard = function()
  if board_ui == nil then return end
  board_ui.gear:release()
  board_ui.world:release()
  board_ui = nil
end

-- **A WORLD MADE AGAIN**, which is the button's job: something to look at, never a thing to keep frozen.
newWorld = function()
  newMap()
  paused = false
  due = 0
  paintPage()
end

-- ---------------------------------------------------------------- the settings

-- **WHAT THE SETTINGS OFFER, AND WHERE THEY LIVE.** Two preferences and nothing else: the wash of the page and the
-- refresh time. They are not the shape of a view, so they are not the `state` session (docs/30, «el que no hi va»): the
-- page opens on the board and a person who comes back finds the two choices they made, which is what a preference is.
--
-- **AND THE PAGE IS THE SYSTEM'S CONTROLS** (l'amo: *«Et recordo que has d'usar els controls del sistema»*), which is the
-- same lesson the page of the time zone learned in 0.174.0: a `ui.checkbox` with its label inside, a `ui.selector` whose
-- door opens a `ui.menu` —the drop-down of this board— and a `ui.button` for the way back. The kernel draws them, the ring
-- of the focus reaches them and the finger is theirs; what this app paints on that page is the paper, the title and three
-- lines of help (docs/30, docs/92).
local Marg = 24                        -- the paper the settings keep on the left and the right
local kWashRowY, kWashH = 116, 48      -- the row of the wash, which is the control's own box
local kTimeY, kTimeH = 240, 56         -- the field of the refresh time
-- **La targeta de la llista és petita de mica i té dues files de 52 com a mínim** (0.189.48): the card of a `ui.menu`
-- keeps sixteen pixels of paper above and below its rows and a row is never under fifty-two, so a card of a hundred and
-- twenty-eight pixels holds **one** row and lays the two options side by side —the finger fell in the gap between the
-- two columns and the click picked the row the dial was on, which is the list that seemed dead (found in the suite,
-- 15/11)—. Two rows of the small face are a hundred and four pixels of rows and thirty-two of paper.
local kMenuX, kMenuY, kMenuW, kMenuH = Marg, 320, 240, 168
local kTitle = scale_for(57)
local kBody = scale_for(43)

-- The boxes the page uses, which are also the boxes `run life keys` prints so a test can press them without knowing the
-- layout (the road of `run tz zones`, docs/85).
local function washRect() return Marg, kWashRowY, PageW - 2 * Marg, kWashH end
local function timeRect() return Marg, kTimeY, PageW - 2 * Marg, kTimeH end
local function menuRect() return kMenuX, kMenuY, kMenuW, kMenuH end

local sett = nil                       -- the controls of the page, made when it opens and given back when it goes

-- **UNA TARGETA NO S'OBRE DINS DEL TOC QUE LA DEMA** (0.189.48): the click of the field arrives with the very finger that
-- pressed it still on the glass, and a card that comes up under that finger takes the same click as a choice of the row
-- the dial was on —the list closed itself and the option never moved (found in the suite, 15/11)—. The field leaves the
-- wish here and the loop opens the card when the glass is clear, which is the rule this project already has for a button
-- that asks for something slow (docs/30, docs/90).
local wantedMenu = nil

-- **Una caixa que ha girat es pinta a si mateixa** (0.179.5): the control marks its box when it draws and the present
-- paints that box, which is what a page of paper that does not move costs.
local function presentASmallBox() ui.present(kTapMode) end

-- **I el mateix amb el desat**: `buildSettings` engega els ganxos del full abans que la funció que escriu el fitxer
-- estigui dita, i un ganxo que tanca sobre un `local function` de més avall tanca sobre un global buit (provat, 15/11).
local saveSettings

local function openTimeMenu(field)
  if sett == nil or sett.menu ~= nil then return end
  local card, why = ui.menu(kMenuW, kMenuH)
  if card == nil then
    field.choosing = false
    console.print('life: no list (' .. tostring(why) .. ')')
    presentASmallBox()
    return
  end
  card.items = { Speeds[1] .. ' ms', Speeds[2] .. ' ms' }
  card.index = speed
  card.scale = kSmall
  card:place(kMenuX, kMenuY)
  card.on_pick = function(self, index)
    -- **Read, close, then do**: the choice is written with the card already gone, so the page under it never moves with a
    -- finger still on the glass (docs/90).
    self:release()
    sett.menu = nil
    speed = index
    field.index = index
    field.choosing = false
    saveSettings()
    ui.focus(field)
    presentASmallBox()
  end
  sett.menu = card
  field.choosing = true
  presentASmallBox()
end

local function releaseSettings()
  wantedMenu = nil
  if sett == nil then return end
  if sett.menu ~= nil then sett.menu:release() end
  -- **The label before the box that holds it**, because a child is not the box's to give back (docs/30).
  if sett.words ~= nil then sett.words:release() end
  if sett.box ~= nil then sett.box:release() end
  if sett.time ~= nil then sett.time:release() end
  if sett.back ~= nil then sett.back:release() end
  sett = nil
end

-- **The controls are born two pixels square and told their box afterwards** (0.174.0): the layout is what gives them the
-- page's boxes, and a page that can be turned says another measure and places them again.
local function buildSettings()
  if sett ~= nil then return true end

  local why
  local box, words, time, back

  box, why = ui.checkbox(2, 2)
  if box == nil then
    console.print('life: no box of the wash (' .. tostring(why or 'the memory') .. ')')
    return false
  end
  words, why = ui.label(2, 2)
  if words == nil then
    box:release()
    console.print('life: no words of the wash (' .. tostring(why or 'the memory') .. ')')
    return false
  end
  words.text = 'Wash every ' .. kWashEvery .. ' gens'
  words.valign = 'middle'
  box:add(words)
  box.checked = washOn
  box.on_focus = function(self, on) remember_the_ring(on, 'wash') end
  box.on_change = function(self, on)
    washOn = on
    saveSettings()
    presentASmallBox()
  end

  time, why = ui.selector(2, 2)
  if time == nil then
    words:release()
    box:release()
    console.print('life: no field of the time (' .. tostring(why or 'the memory') .. ')')
    return false
  end
  time.label = 'Refresh time'
  time.items = { Speeds[1] .. ' ms', Speeds[2] .. ' ms' }
  time.index = speed
  time.on_open = function(self) wantedMenu = self end
  time.on_focus = function(self, on) remember_the_ring(on, 'time') end

  -- **The way back is a button of the system with the `back` mark of the kernel in it**, which is the chevron the whole
  -- system uses for going back a view: it needs no face of the app and it turns over with the press (docs/30, docs/45).
  back, why = ui.button(2, 2)
  if back == nil then
    time:release()
    words:release()
    box:release()
    console.print('life: no back button (' .. tostring(why or 'the memory') .. ')')
    return false
  end
  back.icon = 'back'
  back.on_click = function() closeSettings() end
  back.on_focus = function(self, on) remember_the_ring(on, 'back') end

  sett = { box = box, words = words, time = time, back = back, menu = nil }

  local wx, wy, ww, wh = washRect()
  box:resize(ww, wh)
  box:place(wx, wy)
  box.scale = kSmall
  local line = ui.line_height(kSmall)
  words:resize(ww - box:box() - 32, line)
  words:place(box:box() + 16, (wh - line) // 2)
  words.scale = kSmall

  local sx, sy, sw, sh = timeRect()
  time:resize(sw, sh)
  time:place(sx, sy)
  time.scale = kBody

  local bx, by = worldRect()
  back:resize(Btn, BtnH)
  back:place(bx, by)

  -- **I el cercle torna al control que la persona tenia** (0.189.52, docs/30): a name that is not one of the three is not a
  -- ring, it is the box of the wash, which is where the page starts when nobody has touched it.
  local wanted = { wash = box, time = time, back = back }
  ui.focus(wanted[shape.focus] or box)
  return true
end

-- **THE PAGE OF THE SETTINGS**, in one box and one pass: paper, title, three lines of help, and the controls the system
-- draws —which are asked to draw again because the paper of the page went over what they had there (0.168.4, docs/90)—.
-- The wash is the one that blinks: a page over a board of black cells is the one change on this glass that `gld16` cannot
-- take away (l'amo: *«al canviar a settings hauràs d'usar alguna cosa que netegi molt bé, ara queda brut»*).
local function paintSettings()
  ui.rect(0, 0, PageW, PageH, 15)
  ui.text(Marg, 16, 'Settings', kTitle, 0)
  ui.rect(Marg, 96, PageW - 2 * Marg, 1, 0)
  ui.rect(Marg, kTimeY + kTimeH + 16, PageW - 2 * Marg, 1, 0)
  -- **What the hand can do, in three short lines**: the strip used to say it and no longer has the room for it, and the
  -- longest of these is nineteen characters, because the face of the flash is the widest this page ever sees (docs/50).
  ui.text(Marg, 396, 'Tap a cell to paint', kSmall, 0)
  ui.text(Marg, 436, 'Center: pause', kSmall, 0)
  ui.text(Marg, 476, 'Sides: the time', kSmall, 0)
  ui.text(Marg, 528, 'A board of ' .. Cols .. ' by ' .. Rows, kSmall, 0)

  if sett ~= nil then
    for _, one in ipairs({ sett.box, sett.time, sett.back }) do one:invalidate() end
  end
  ui.present(kSettMode)
end

-- **The two preferences, in the app's own folder** (`fs.data`, docs/70): a version and a line per choice, so a file of
-- another shape is thrown away instead of read as this one (the rule of the `state` session, docs/30).
saveSettings = function()
  local ground = fs.data()
  if ground ~= nil then pcall(fs.mkdir, ground) end
  local written, reason = fs.write(fs.data(kSettingsFile),
    string.format('v1\nwash=%d\nspeed=%d\n', washOn and 1 or 0, speed))
  if written == nil then console.print('life: the settings were not written: ' .. tostring(reason)) end
end

local function loadSettings()
  local text = fs.read(fs.data(kSettingsFile))
  if text == nil then return end
  local first = true
  for row in text:gmatch('[^\n]+') do
    if first then
      first = false
      if row ~= 'v1' then return end
    else
      local key, value = row:match('^([%w_]+)=(%d+)$')
      if key == 'wash' then
        washOn = (value == '1')
      elseif key == 'speed' then
        speed = math.min(math.max(1, tonumber(value) or #Speeds), #Speeds)
      end
    end
  end
end

-- **A STOPPED BOARD UNDER THE SETTINGS**: the world does not run while a person is choosing what to do with it, and it
-- comes back as it was —running or stopped— when the settings go away.
openSettings = function()
  if setting then return end
  if not buildSettings() then return end
  releaseBoard()
  setting = true
  shape.page = 2
  save_state(shape)
  wasPaused = paused
  paused = true
  paintSettings()
end

closeSettings = function()
  if not setting then return end
  releaseSettings()
  setting = false
  shape.page = 1
  save_state(shape)
  paused = wasPaused
  due = 0
  buildBoard()
  paintPage(kSettMode)
end

-- ------------------------------------------------------------------- the hand

-- A finger on a cell turns it over, and the next generation comes out of that: it is the most direct way to try
-- everything, and whoever wants a glider can paint one.
local function tap(x, y)
  -- **The page of the settings is another page and its finger is the system's**, and so is the finger of the two buttons
  -- of the strip: a checkbox, a door and three buttons of the kernel take the contact by themselves, so what this app
  -- hit-tests is the board and nothing else (0.189.48, docs/30).
  if setting then return end

  if y < GridY or y >= GridY + GridH then return end
  if x < GridX or x >= GridX + GridW then return end

  local row = math.floor((y - GridY) * Rows / GridH) + 1
  local col = math.floor((x - GridX) * Cols / GridW) + 1
  if row < 1 or row > Rows or col < 1 or col > Cols then return end

  local k = index(row, col)
  if alive[k] == 1 then
    alive[k] = 0
    live = live - 1
  else
    alive[k] = 1
    live = live + 1
  end

  paintAround(row, col)
  paintCounters()
  ui.present(kGenMode)
  statusDirty = false
  washes = washes + 1
end

-- **Which key is which**, read once from the board: the face and the spot are the panel's, and a board with no keys
-- answers an empty table (docs/30).
local spots = {}
for _, key in ipairs(button.list()) do spots[key.key] = key.spot end

-- **The keys are the app's**: the centre pauses and gives the clock back, and the two sides pick the speed. Nothing
-- is painted here —the hook runs with the coroutine suspended—: the loop paints the strip when it is due.
function on_button(kind, held_ms, key)
  if kind ~= 'press' then return end

  -- **A page with controls has the keys of the system**, so this hook does not hear them while the settings are open —the
  -- ring walks the checkbox, the field and the button, and the centre presses the one it is on—. What is kept is one way
  -- out for a page whose controls could not be made at all: the centre goes back (docs/30).
  if setting then
    if spots[key] == 'centre' and sett == nil then closeSettings() end
    return
  end

  local spot = spots[key]
  if spot == 'centre' then
    paused = not paused
    if not paused then due = 0 end
    statusDirty = true
  elseif spot == 'left' or spot == 'down' then
    if speed > 1 then
      speed = speed - 1
      statusDirty = true
    end
  elseif spot == 'right' or spot == 'up' then
    if speed < #Speeds then
      speed = speed + 1
      statusDirty = true
    end
  end
end

-- ------------------------------------------------------------------- the run

local start = ui.caps()
local W, H = start.width, start.height
PageW, PageH = W, H
measure(W, H)
loadSettings()

local arg = (app.args ~= nil) and app.args.path or ''
-- **WITH `blank` THE GRID STARTS EMPTY AND STOPPED**: it is there so a pattern can be painted by hand (a glider, a gun)
-- and so a test can count cells. Stopped, because a generation of an empty world is a new map —that is the boredom rule
-- of `step`— and the hand would never get to paint the first cell. With no argument the seed is random and what comes out
-- is a soup.
if arg == 'blank' then
  paused = true
else
  newMap()
end

if arg == 'keys' then
  console.print(string.format('Life grid %d %d %d %d %d %d', Cols, Rows, GridX, GridY, GridW, GridH))
  local bx, by = worldRect()
  console.print(string.format('Life button %d %d %d %d', bx, by, Btn, BtnH))
  local gx, gy = gearRect()
  console.print(string.format('Life gear %d %d %d %d', gx, gy, Btn, BtnH))
  local wx, wy, ww, wh = washRect()
  console.print(string.format('Life wash %d %d %d %d', wx, wy, ww, wh))
  local sx, sy, sw, sh = timeRect()
  console.print(string.format('Life time %d %d %d %d', sx, sy, sw, sh))
  local mx, my, mw, mh = menuRect()
  console.print(string.format('Life menu %d %d %d %d', mx, my, mw, mh))
  console.print(string.format('Life pref %d %d', washOn and 1 or 0, speed))
  console.print(string.format('Life cell %d', Cell))
  console.print(string.format('Life speeds %d %d', Speeds[1], Speeds[2]))
end

-- **`settings` obre l'app a la pàgina dels ajustos**, que és com es mira des de l'emulador sense un dit a sobre.
-- **La forma que la persona va deixar, llegida abans de la primera pàgina** (0.189.52, docs/30): `arg` is the prompt asking
-- for the settings page and it wins over the block, because a person who names a page is a person who wants it.
local was = load_state()
if was ~= nil then
  local page = tonumber(was.page)
  if page == 2 then shape.page = 2 end
  if was.focus ~= nil and was.focus ~= '' then shape.focus = was.focus end
end

buildBoard()
if arg == 'settings' or shape.page == 2 then
  paintPage()
  openSettings()
else
  paintPage()
end

-- **Les tres portes del comiat** (docs/30, docs/40): the shape is saved here as well, because an app does not always see the
-- end coming —a raise, the ESC, a `poweroff`— and a page that is only written when it changes loses the last change.
function on_pause() save_state(shape) end
function on_save() save_state(shape) end
function on_close() save_state(shape) end

local held = false

while true do
  -- **The room of the view is what the grid is measured from**: the system can give the app another view —the whole
  -- glass, a turn, a smaller bar— and the world is moved into the new one and painted whole (docs/30, docs/40).
  local view = ui.caps()
  if view.width ~= W or view.height ~= H then
    W, H = view.width, view.height
    PageW, PageH = W, H
    measure(W, H)
    paintPage()
  end

  -- **The finger is a contact and not a gesture**: a cell is painted on the first frame that sees the finger, so a
  -- shape can be drawn quickly and two cells a moment apart are two cells and not a `double` (docs/45, trap 4).
  local fingers = touch.read()
  if #fingers > 0 then
    if not held then
      held = true
      tap(fingers[1].x, fingers[1].y)
    end
  elseif held then
    held = false
  end

  -- **I la targeta s'obre amb el vidre buit** (0.189.48): the wish the field left is done here, where no finger can take the
  -- click of the row it lands on.
  if wantedMenu ~= nil and not held then
    local field = wantedMenu
    wantedMenu = nil
    openTimeMenu(field)
  end

  -- **The gestures of the system are taken and dropped**: the tap and the double of this frame are the finger above,
  -- and a queue nobody empties is a queue that grows (docs/30).
  local gesture = touch.gesture()
  while gesture do gesture = touch.gesture() end

  -- A key changes the counters and they are painted here, once, when their text is another one.
  if statusDirty and not setting then
    paintCounters()
    paintSpeed()
    ui.present(kGenMode)
    statusDirty = false
  end

  -- **A STOPPED GRID HAS NO WORK**: a paused game does not ask for a generation, and a turn that only looks at the
  -- clock costs a Lua call and nothing else.
  if not paused then
    local now = sys.time_ms()
    if now >= due then
      due = now + Speeds[speed]
      step()
      refresh()
    end
  end

  sys.delay(20):await()
end
