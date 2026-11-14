-- life: Conway's game of life, on electronic ink.
--
-- Ported from the paper project (`data/app/Life/pos_launch.lua`, version 1.5.2) with **the screen and the behaviour
-- whole** and the original's numbers (docs/45). A grid of thirty-pixel cells, two generations and a rule of three
-- lines; four speeds, a pause, a `New` button and a finger that paints the world one cell at a time.
--
-- **EVERY GENERATION IS A PASS OF THE PANEL, AND THAT IS THE CLOCK.** That is why the speed is given in
-- MILLISECONDS —1200, 700, 400 and 250— and why the ceiling is the glass and not the processor: a generation that
-- paints nothing is a generation nobody sees.
--
-- **TWO BOARDS AND NOTHING ELSE.** The new one is computed over the old —it cannot be done in place, because a cell
-- that changes would affect its neighbours— and at the end the two swap roles. No new Lua table per generation: the
-- two tables are made once, in `measure`, and filled with 0 and 1.
--
-- **THE WORLD IS A TORUS**: whoever leaves on the right comes back in on the left. Shorter to write and more fun to
-- watch than an edge that kills.
--
-- **AND THE REFRESH IS ONLY WHAT CHANGED.** A generation changes a handful of cells, and those are what travels to
-- the panel: the app draws the changed rows —and the row above and below each, because the line between two cells is
-- drawn INSIDE the one above and the one on the left— and names the pass `a2`, two inks and 290 ms. Drawing the whole
-- board would be a page of sixteen levels every generation, which is what made this game impossible to watch on the
-- paper (docs/50, docs/45).
--
-- **A SOUP IS THE EXCEPTION, AND IT IS THE PANEL'S RULE AND NOT THIS APP'S.** While almost every row of the world
-- changes, the box of the damage is the whole glass, and this board does not take a two-level pass of the whole glass:
-- it goes up as a `gc16`, four hundred and fifty milliseconds, which is what the paper measured too (a new map changes
-- 88 % of the panel there, docs/50). As the world settles the pass shrinks to the rows that still move and it is the
-- cheap one again.
--
-- **THE SQUARES OF A SHAPE ARE SEEN.** Two cells that touch share their edge, so without a line a shape is one black
-- block. What shows them is a ONE-pixel line of paper between two cells that are BOTH alive —and only then—, drawn
-- after the fill. Erasing a pixel next to a dead cell would leave a row of paper that can never change, and the rows
-- that can change are what buys the clean pass of a new map (the original's T99).
--
-- **NO CONTROL TREE AND ONE BUTTON PAINTED BY HAND.** The page has no focusable control, so the dial of the board is
-- the app's: the centre pauses, the two sides change the speed, and none of the three costs a control. The `New` of
-- the strip is a frame and a word, and the same rectangle both paints it and takes the finger. The centre key is
-- also the system's way out —three presses inside a second and a half close the app— and a hook cannot take it away.
--
-- **WHAT IS NOT REMEMBERED, AND WHY.** The world, the speed and the pause are the game and the person's choice, and
-- the original threw them away when the app closed; this port does the same. There is no `state` session because a
-- grid with no control has no shape left behind to rebuild: the board comes back as a new world, which is what the
-- paper did (docs/45 §5 bis).

local caps = sys.caps()
if not caps.display then
  console.print('this board needs a glass for this')
  return
end

local Cell = 30                        -- the size a cell aims for, in pixels
local Speeds = { 1200, 700, 400, 250 } -- how long a generation lasts, in ms
local Top = 72                         -- the strip at the top: status and button
local BtnW = 84                        -- the New button, in pixels, and its
local BtnH = 63                        -- ...height is the touch minimum

-- **The two qualities this page asks for**: the whole world in a pass of sixteen levels that does not blink when it
-- opens, and a generation in the two-level pass, which is the fastest one that lands on the level. A `a2` of the
-- WHOLE view is the one shape this panel did not take (docs/50), and a generation is a band of rows and not the view;
-- when a soup makes that band the whole glass the kernel's own rule gives it the wash and this app has no vote.
local kPageMode = 'gl16'
local kGenMode = 'a2'

-- **The size of the paper's face, in pixels of line** (docs/45): small is 33. Asking for the size and not the step
-- keeps the page the same physical size on both projects (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  return math.max(1, math.min(16, math.floor(line_px / step + 0.5)))
end

local kSmall = scale_for(33)

local GridW, GridH                     -- the area the grid gets, in pixels
local Cols, Rows                       -- how many cells fit in it
local alive, fresh                     -- the two boards (0 and 1)
local gen, live = 0, 0                 -- counters
local paused = false
local due = 0                          -- when the next generation is due
local speed = 2
local whole = true                     -- true when what is on the glass is a brand new board

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

-- The cells that fit in an area of this size. The strip keeps what it needs and the grid takes the REST.
local function fits(w, h)
  return math.max(4, math.floor(w / Cell + 0.5)),
    math.max(4, math.floor((h - Top) / Cell + 0.5))
end

-- THE GRID, MEASURED FROM THE SPACE THAT IS THERE, and what was there is MOVED and not thrown away: a turn of the
-- glass must not kill the game (the original's `measure`).
local function measure(w, h)
  local oldBoard, oldCols, oldRows = alive, Cols, Rows

  GridW, GridH = w, h - Top
  Cols, Rows = fits(w, h)

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
  local x0 = bound(col - 1, Cols, GridW)
  local x1 = bound(col, Cols, GridW)
  local y0 = Top + bound(row - 1, Rows, GridH)
  local y1 = Top + bound(row, Rows, GridH)
  return x0, y0, x1, y1
end

-- **ONE CELL, AND THE LINES THAT ARE ITS**: the fill, and the paper line that separates it from the cell on its
-- right and from the one below —both drawn inside this cell's own box— when the two are alive (the original's T99).
-- The line against the left cell and the one above are painted by that cell, so every drawing of a cell is
-- accompanied by its neighbours in `paintAround` and `paintRows` and no line is left behind.
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

-- The rows a generation changed, plus the row above and the one below each: the line between two rows is drawn
-- inside the one above, which is not painted otherwise. Consecutive rows become one band.
local function paintRows()
  for i = 1, nDirty do
    local row = dirtyRows[i]
    for r = math.max(1, row - 1), math.min(Rows, row + 1) do
      for c = 1, Cols do paintCell(r, c) end
    end
  end
end

-- The whole world, corner to corner.
local function paintGrid()
  for row = 1, Rows do
    for col = 1, Cols do paintCell(row, col) end
  end
end

local function statusText()
  return string.format('gen %d   %d alive   %s', gen, live,
    paused and 'paused' or (Speeds[speed] .. ' ms'))
end

-- THE STATUS OF THE STRIP: the counters and, under them, what the hand can do. It is papered first —a counter that
-- grows is a shorter number over a longer one— and it stops where the button begins, because the button is painted
-- by its own function.
local function paintStatus()
  local btn_x = GridW - BtnW - 8

  ui.rect(0, 0, btn_x, Top, 15)
  ui.text(12, 4, statusText(), kSmall, 0)
  ui.text(12, 38, 'Tap: a cell   Center: pause', kSmall, 0)
end

-- THE BUTTON, AND IT LIVES IN THE STRIP. Over the grid it would be a box of rows that never change.
local function buttonRect()
  return GridW - BtnW - 8, 4, BtnW, BtnH
end

local function paintButton()
  local x, y = buttonRect()

  ui.frame(x, y, BtnW, BtnH, 0)
  ui.text(x + math.floor((BtnW - ui.text_width('New', kSmall)) / 2), y + 15, 'New', kSmall, 0)
end

-- The page: the whole glass in one pass of sixteen levels that does not blink, when the app opens, when the view is
-- another one, and when the world is new.
local function paintPage()
  ui.clear(15)
  paintGrid()
  paintStatus()
  paintButton()
  ui.present(kPageMode)
  whole = false
  statusDirty = false
end

-- What a generation costs: the rows it touched and the strip of the counters, in the two-level pass.
local function refresh()
  if whole then
    paintPage()
    return
  end

  paintRows()
  paintStatus()
  ui.present(kGenMode)
  statusDirty = false
end

-- ------------------------------------------------------------------- the hand

-- A finger on a cell turns it over, and the next generation comes out of that: it is the most direct way to try
-- everything, and whoever wants a glider can paint one.
local function tap(x, y)
  local bx, by = buttonRect()

  if x >= bx and x < bx + BtnW and y >= by and y < by + BtnH then
    -- THE BUTTON: A NEW MAP, and playing again if it was paused. A new map is something to look at, never a thing
    -- to keep frozen.
    newMap()
    paused = false
    due = 0
    paintPage()
    return
  end

  if y < Top then return end

  local row = math.floor((y - Top) * Rows / GridH) + 1
  local col = math.floor(x * Cols / GridW) + 1
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
  paintStatus()
  ui.present(kGenMode)
  statusDirty = false
end

-- **Which key is which**, read once from the board: the face and the spot are the panel's, and a board with no keys
-- answers an empty table (docs/30).
local spots = {}
for _, key in ipairs(button.list()) do spots[key.key] = key.spot end

-- **The keys are the app's**: the centre pauses and gives the clock back, and the two sides pick the speed. Nothing
-- is painted here —the hook runs with the coroutine suspended—: the loop paints the strip when it is due.
function on_button(kind, held_ms, key)
  if kind ~= 'press' then return end

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
measure(W, H)

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
  local bx, by = buttonRect()
  console.print(string.format('Life grid %d %d %d %d', Cols, Rows, GridW, GridH))
  console.print(string.format('Life button %d %d %d %d', bx, by, BtnW, BtnH))
  console.print(string.format('Life cell %d', Cell))
  console.print(string.format('Life speeds %d %d %d %d', Speeds[1], Speeds[2], Speeds[3], Speeds[4]))
end

paintPage()

local held = false

while true do
  -- **The room of the view is what the grid is measured from**: the system can give the app another view —the whole
  -- glass, a turn, a smaller bar— and the world is moved into the new one and painted whole (docs/30, docs/40).
  local view = ui.caps()
  if view.width ~= W or view.height ~= H then
    W, H = view.width, view.height
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

  -- **The gestures of the system are taken and dropped**: the tap and the double of this frame are the finger above,
  -- and a queue nobody empties is a queue that grows (docs/30).
  local gesture = touch.gesture()
  while gesture do gesture = touch.gesture() end

  -- A key changes the strip and the strip is painted here, once, when its text is another one.
  if statusDirty then
    paintStatus()
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
