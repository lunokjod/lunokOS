-- snake: the snake of the phones, on electronic ink.
--
-- **IT IS THE SNAKE OF A NOKIA**, the one that travelled with the 6110 in 1998, and of it this port keeps the three rules
-- the genre wrote down: the snake never stops and never shrinks —the head takes a cell per step and the tail gives one up—,
-- one piece of food is one cell more, and a wall or its own body ends the game. There is no wrap-around: a field whose edges
-- are doors is another game.
--
-- **THE FIELD IS TWENTY BY TWENTY AND THE SCORE IS ONE POINT PER PIECE**, which is the number a player counts. The pace comes
-- out of a level: `StepMsStart - StepMsPerLevel * level` with a floor of `StepMsFloor`, and the level goes up when the pieces
-- of the level are there —`PointsPerLevel * (level + 1)`—, so the bar of the strip and the number of the level are the same
-- number and neither of them can lie (the rule the old project wrote for its own Snake and for the Tetris before it).
--
-- **AND THERE IS NO GAMEPAD** (the owner, 22/11: *«Fes el port de l'snake, però no posis el comandament de joc. Usarem la
-- pantalla per orientar el cuc. Tocs.»*). The old app steered with the cross, the dial and two capsules of the system's pad;
-- this one has the field and nothing else: **a tap says which way the head has to go from where it is**, the axis with more
-- travel decides, and a tap that points straight back is read as the sideways half of it —a finger that means «turn» and
-- pushes back is not a finger asking for a wall—. A turn that would reverse the snake does not happen at all (the rule the
-- old project's Tetris wrote: *a turn that does not fit DOES NOT HAPPEN, and the finger gets nothing*). **I un toc durant una
-- demostració del CPU la pren**: the finger stops being a direction and starts being the person again.
--
-- **THE REDRAW IS THE WHOLE POINT OF THIS APP** (the owner: *«Aquesta app es especialment complexa perquè el redibuixat està
-- milimetrat per no fer parpallejos i anar el més ràpid possible»*). Three policies, and none of them is «paint the page»:
--
--   - **A step paints the cells it changed and nothing else**: the cell the head has arrived at, the cell the tail has left,
--     the food that is eaten and the food that arrives. That is two to four boxes in **one** present, and a present of a box
--     is 120 ms of `du4` —four greys and no flash—, which is the waveform the old app measured for its own steps. A page of
--     sixteen greys on every move of the snake is a blink per cell.
--   - **The strip is painted when it says something else**: the score, the level, the bar and the speed are a box of their
--     own, in `gld16` (it is text, and the greys of a letter have to land on themselves) and only when the number moves.
--   - **The page is painted once** —the paper, the wall, the worm and the food— in `gl16`, when the app opens, when the view
--     changes its size and when a game begins: what does not move is not painted again.
--
-- **AND THE CPU PLAYS TOO**, which is the old app's `DEMO`: the same field, the same clock and the same rules, and what changes
-- is who chooses the direction. It is two rules —what it wants is the piece of food and what it cannot do is shut itself in— and
-- the room is counted with the flood fill every game of a grid has, with the generation marks the old app paid for. Its level is
-- ITS OWN and climbs as it eats, because a demo that climbed the person's ladder would be a demo of the person's game; and **a
-- tap on the field hands it back**, which is what the old app did with any key. The door is a third button of the strip, `Demo`,
-- because this page has no card of the system to ask.
--
-- **AND THE TWO THINGS THE OLD APP HAD THAT THIS ONE DOES NOT**: its `canvas` and the seven greys of its trail. This system
-- merges the boxes of one present in the kernel and owns the ghost threshold of the panel (docs/50), so neither the canvas nor
-- the periodic clean of the old app is the app's business here.
--
-- **THE SEGMENTS ARE SEEN**: one pixel of paper between two cells of the worm that touch and only between them, which is the
-- detail that turns a black worm into a worm (the same idea the old project's Life paid for). And **a piece of food is
-- smaller than a cell**, so what fills a whole cell is always the worm.
--
-- **THE GAME IS DATA AND WHAT A TAP MEANS IS FORM**: the worm, the food and the score live in this app's own file
-- (`fs.data('game.txt')`) and come back where they were; whether the page is paused goes to the `state` block, because a
-- pause is the shape of the view a person left behind (docs/30, docs/45 §5 bis).

local Cols, Rows = 20, 20        -- the field of this board, in cells

-- **The ladder of the level, and the pace comes out of it**: 220 ms at level zero and 70 at the top of it, which is level
-- ten. The three numbers are the old app's own (its `StepMsStart`, `StepMsPerLevel` and `StepMsFloor`), and the floor is
-- there because a hand has a limit and a snake faster than an eye is a game nobody is playing any more.
local StepMsStart = 220
local StepMsPerLevel = 15
local StepMsFloor = 70
local LevelMax = 10

-- What a level asks for, in pieces: `PointsPerLevel` x `(level + 1)`, which is the rule the old project's Tetris wrote (a
-- level that asks for the same at every step of the ladder is a level that goes up in seconds at the top of it).
local PointsPerLevel = 5
local StartLen = 3               -- a worm of three, the one of the phone

-- **The CPU of the demo, which is the same game and the same clock**, and what changes is who chooses the direction (the old
-- app's `DEMO`, and its `CpuLookAhead`): beside the person's level it has one of its OWN, because a demo that climbed the
-- person's ladder would be a demo of the person's game (docs/30, docs/45).
local CpuLookAhead = 40          -- cells the flood fill counts before the CPU decides

-- **The three tones of the game**: the ink, the wall and the paper. They are tones of the mode the app paints in —`du4`
-- carries four greys and nothing more—, so a tone that is not one of them is a tone the panel invents.
local Ink, Wall, Paper = 0, 5, 15

-- **The three qualities of a paint** (docs/50): the page is sixteen levels and one pass, a step is the four greys that carry
-- no flash, and what is text —the strip and the line of the bottom— is dealt with in the quality that lands the greys of a
-- letter on themselves.
local kPageMode = 'gl16'
local kStepMode = 'du4'
local kTextMode = 'gld16'

local Strip = 56                 -- the strip of the score above the field
local Footer = 44                -- the line of the bottom: what the game is saying
local Margin = 12                -- the paper on the sides
local WallPx = 3                 -- the wall of the field, in pixels of ink
local BtnH = 63                  -- the height of a button, the least a finger can hit
local BtnPad = 16                -- the room a button leaves its word on each side
local BtnMinW = 84               -- never narrower than this, even when the word would fit in less
local BarH = 6                   -- the level's bar, inside the strip
local BarBorder = 1
local FileName = 'game.txt'      -- the game, inside this app's own folder
local BestName = 'best.txt'      -- the best score, which is a preference and not the game
local kTapPx = 10                -- a tap shorter than this from the head is a tap that says nothing

-- **The sizes of the paper's face, in pixels of line** (docs/45): small 33 and body 43, asked for by the size and not by the
-- step, so the page keeps the same physical size as the other apps (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  return math.max(1, math.min(16, math.floor(line_px / step + 0.5)))
end

local kSmall = scale_for(33)
local kBody = scale_for(43)

-- ---------------------------------------------------------------- what two functions share

-- **Tot el que dues funcions comparteixen es declara aquí**, and not next to the first one that wants it: a `local` after a
-- function is a global inside it, and this file learned it the way every Lua file does (docs/30, docs/45).

local state = nil                -- the `state` block: asked once and kept for the life of the app
local paused = false             -- the shape of the view: the game is stopped on the screen

local body = {}                  -- the worm, flat cells: `body[1]` is the head
local dirX, dirY = 1, 0          -- and where it is going
local wantX, wantY = nil, nil    -- the turn the finger has asked for, taken on the next step
local food = 0                   -- the cell of the piece of food, 0 when the field is full
local score = 0
local level = 0
local levelBase = 0              -- the pieces at which the level in march began
local over = false
local why = ''                   -- 'wall', 'tail' or 'full': what ended the game
local best = 0
local demo = false               -- the CPU is playing this game and not the person
local cpuLevel = 0               -- its own step of the ladder
local cpuPointsBase = 0          -- and where its level began

local cell, fieldX, fieldY, fieldW = 0, 0, 0, 0
local stripH = Strip              -- the height of the strip, measured from the face of the paper
local scoreW = 0                  -- and how much of it is left between the two buttons
local barY = 0                    -- where the level's bar sits, which follows the two rows above it
local W, H = 0, 0

-- **La línia d'estat es declara aquí i s'escriu més avall** (the house's own rule, docs/45): `tapAt` l'ha de poder dir —un botó
-- que comença una demostració ho ha de dir, o una prova ha d'endevinar-ho mirant la pàgina— i una funció que es declara després
-- del seu lector és un global dins d'ell.
local stateLine

local the_score, the_speed, the_foot, the_pause, the_new, the_demo
local pauseX, pauseY, pauseW, newX, newY, newW, demoX, demoW
local lastScore, lastLevel, lastFoot = -1, -1, ''

-- ---------------------------------------------------------------- the state of the page (form)

local kStateBytes = 1024
local kLayout = 'v1'             -- bumped when a control is renamed or the page changes shape

local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end

  return state
end

-- **What the page is now**: whether it is stopped, and nothing else. The worm is the game and it lives in `fs.data`, because
-- a game is data and a pause is the shape of the page (docs/30, docs/45).
local function save_state()
  local block = the_state()
  if block == nil then return false end

  block:clear()
  block:write(0, kLayout .. '\npaused=' .. (paused and '1' or '0'))

  return true
end

-- **What the page was**: `nil` when there is nothing to read back or the block is of another shape, which is a page that
-- starts as a new one and not a ring on a control that no longer exists.
local function load_state()
  local block = the_state()
  if block == nil then return nil end

  local text = block:read(0, kStateBytes) or ''
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end

  local now = {}
  local first = true

  for line in text:gmatch('[^\n]+') do
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

-- ---------------------------------------------------------------- the game

local function idx(row, col) return (row - 1) * Cols + col end
local function rowOf(i) return math.floor((i - 1) / Cols) + 1 end
local function colOf(i) return ((i - 1) % Cols) + 1 end

local function inField(row, col)
  return (row >= 1) and (row <= Rows) and (col >= 1) and (col <= Cols)
end

-- **A piece of food, in one of the cells the worm is not**: the list is walked once and the cell is drawn from it with the
-- machine's own random numbers, which is what `seed=` fixes for a test.
local free = {}

local function placeFood()
  local used = {}
  local n = 0

  for i = 1, #body do used[body[i]] = true end

  for i = 1, Cols * Rows do
    if not used[i] then
      n = n + 1
      free[n] = i
    end
  end

  if n == 0 then
    -- **The field is full**: there is nowhere left to put anything, and that is the only way this game is won.
    food = 0
    return false
  end

  food = free[math.random(n)]

  return true
end

-- ---------------------------------------------------------------- the CPU of the demo

-- **What it wants is the food and what it cannot do is shut itself in.** The first rule alone is a worm that eats itself in a
-- corner —the head goes straight at the food and the body arrives behind it—, so every way it could take is measured by the
-- **room it leaves**: the cells still reachable from there, counted with the flood fill every game of a grid already has. It is
-- a greedy with a look at the room, which is what a snake can be; the perfect game is a road that visits every cell, and that is
-- not a demo of four hundred cells in Lua on this board (docs/45).
--
-- **And nothing of this allocates**: the body is marked once per step by a generation, the fill marks the cells the same way —a
-- table that is never cleared, because a cell is «seen» when its mark is the mark of this fill— and the stack is a list of
-- numbers that is reused. Rubbish per step is what fills the memory of a machine that has to run a step every seventy
-- milliseconds.
local Ring = {
  { x = 0, y = -1 },
  { x = 1, y = 0 },
  { x = 0, y = 1 },
  { x = -1, y = 0 },
}

local cpuBlocked = {}            -- cell -> the generation that blocked it
local cpuSeen = {}               -- and the same for the flood fill
local cpuStack = {}              -- reused: the cells the fill has to visit
local cpuGen = 0                 -- the generation of the BODY of this step
local cpuSeenGen = 0             -- and the generation of ONE fill (see below)

-- **Every fill has a generation of its own**, which is not a detail: the four ways of a step are measured one after the other and
-- the marks of the fill before are still in the table. With one generation for all of them, the second way found its neighbours
-- already seen and answered «one cell» —the throw of a dice that left the old CPU going round in circles, measured there as two
-- pieces of food in four hundred steps (docs/45).
local function cpuRoom(row, col)
  if not inField(row, col) then return 0 end

  local start = idx(row, col)
  if cpuBlocked[start] == cpuGen then return 0 end

  cpuSeenGen = cpuSeenGen + 1

  local top = 1
  cpuStack[1] = start
  cpuSeen[start] = cpuSeenGen

  local n = 0

  while (top > 0) and (n < CpuLookAhead) do
    local at = cpuStack[top]
    top = top - 1
    n = n + 1

    local r, c = rowOf(at), colOf(at)

    for d = 1, #Ring do
      local rr, cc = r + Ring[d].y, c + Ring[d].x
      if inField(rr, cc) then
        local nextCell = idx(rr, cc)
        if (cpuBlocked[nextCell] ~= cpuGen) and (cpuSeen[nextCell] ~= cpuSeenGen) then
          cpuSeen[nextCell] = cpuSeenGen
          top = top + 1
          cpuStack[top] = nextCell
        end
      end
    end
  end

  return n
end

-- **What it does with a step**: the way that eats, and if none eats the way that leaves the most room, and if two leave the same
-- the one that gets closer to the food. The way back is not a way, and the tail counts as room because it gives up its cell on
-- this very step (the old app's `cpuChoose`, and this port keeps its two rules and its tie-breaks).
local function cpuChoose()
  cpuGen = cpuGen + 1

  for i = 1, #body do cpuBlocked[body[i]] = cpuGen end
  cpuBlocked[body[#body]] = nil

  local headRow, headCol = rowOf(body[1]), colOf(body[1])
  local best, bestRoom, bestDist, bestEats = nil, -1, 0, false

  for d = 1, #Ring do
    local dir = Ring[d]

    if not ((#body > 1) and (dir.x == -dirX) and (dir.y == -dirY)) then
      local row, col = headRow + dir.y, headCol + dir.x

      if inField(row, col) then
        local room = cpuRoom(row, col)

        if room > 0 then
          local eats = (idx(row, col) == food)
          local dist = 0
          if food > 0 then dist = math.abs(col - colOf(food)) + math.abs(row - rowOf(food)) end

          local better = false
          if best == nil then
            better = true
          elseif eats and not bestEats then
            better = true
          elseif eats == bestEats then
            if room > bestRoom then
              better = true
            elseif (room == bestRoom) and (dist < bestDist) then
              better = true
            end
          end

          if better then best, bestRoom, bestDist, bestEats = dir, room, dist, eats end
        end
      end
    end
  end

  return best or { x = dirX, y = dirY }
end

local function levelPointsFor(lv) return PointsPerLevel * (lv + 1) end

local function stepMs()
  local who = demo and cpuLevel or level
  if who > LevelMax then who = LevelMax end

  return math.max(StepMsFloor, StepMsStart - StepMsPerLevel * who)
end

-- **The level goes up when the pieces of the level are there**, and the pace comes out of it: it is not a piece of food that
-- makes the worm faster, it is a step of the ladder —which is what the bar of the strip says.
local function raiseLevel()
  -- **I el CPU té la seva pròpia escala** (`cpuLevel` i la seva base): el mateix joc, el mateix rellotge i la mateixa regla, i el
  -- que canvia és qui tria el camí —una demostració que pugés amb el nivell de la persona seria una demostració del joc d'ella—.
  if demo then
    while (score - cpuPointsBase) >= levelPointsFor(cpuLevel) do
      cpuPointsBase = cpuPointsBase + levelPointsFor(cpuLevel)
      cpuLevel = cpuLevel + 1
    end

    return
  end

  while (score - levelBase) >= levelPointsFor(level) do
    levelBase = levelBase + levelPointsFor(level)
    level = level + 1
  end
end

-- How much of the level in march is played, between 0 and 1: it is the bar.
local function levelProgress()
  local who = demo and cpuLevel or level
  local base = demo and cpuPointsBase or levelBase
  local need = levelPointsFor(who)
  if need <= 0 then return 0 end

  local got = (score - base) / need
  if got < 0 then return 0 end
  if got > 1 then return 1 end

  return got
end

local function newGame()
  body = {}

  local row = math.floor(Rows / 2)
  local col = math.floor(Cols / 2)
  for i = 1, StartLen do body[i] = idx(row, col - (i - 1)) end

  dirX, dirY = 1, 0
  wantX, wantY = nil, nil
  score, level, levelBase = 0, 0, 0
  over, why = false, ''

  placeFood()
end

-- **And what ends a game says why**: a wall and its own tail are two different deaths, and the line of the bottom is the only
-- thing that tells them apart on a page with no card (docs/30).
local function finish(reason)
  over = true
  why = reason
  if score > best then
    best = score

    local ground = fs.data()
    if ground ~= nil then pcall(fs.mkdir, ground) end

    local written = fs.write(fs.data(BestName), tostring(best))
    if written == nil then console.print('[snake] the best score did not fit on the disk') end
  end
end

-- ---------------------------------------------------------------- the step

-- **One step of the worm.** It fills `changed` with the cells that are not the same as they were, which is what the panel
-- has to be told about, and it answers whether the game goes on.
local changed = {}

local function step()
  changed = {}

  -- **El CPU tria el camí del pas, i el mateix `step` el segueix**: the demo is not a second game and not a second step — it is
  -- this one with the direction chosen by `cpuChoose` (the old app's `DEMO`, docs/45).
  if demo then
    local pick = cpuChoose()
    wantX, wantY = pick.x, pick.y
  end

  -- **The turn the finger asked for, if it still fits**: it is taken here and not when the tap lands, because a turn that
  -- arrives between two steps is a turn that happened while the worm was moving (docs/30).
  if wantX ~= nil then
    if not ((wantX == -dirX) and (wantY == -dirY)) then
      dirX, dirY = wantX, wantY
    end
    wantX, wantY = nil, nil
  end

  local headRow, headCol = rowOf(body[1]), colOf(body[1])
  local row, col = headRow + dirY, headCol + dirX

  if not inField(row, col) then
    finish('wall')
    return false
  end

  local cellNew = idx(row, col)
  local ate = (cellNew == food)

  -- **Its own body ends the game, and the last cell does not count when the worm is not growing**: it is the one that gives up
  -- its place on this very step, so a head that arrives there arrives where the tail no longer is. Counting it would end a
  -- game the phone lets go on.
  local tail = ate and 0 or body[#body]

  for i = 1, #body do
    if (body[i] == cellNew) and (body[i] ~= tail) then
      finish('tail')
      return false
    end
  end

  changed[#changed + 1] = body[1]
  table.insert(body, 1, cellNew)
  changed[#changed + 1] = cellNew

  if ate then
    score = score + 1
    raiseLevel()
    changed[#changed + 1] = food

    if not placeFood() then
      finish('full')
      return false
    end

    changed[#changed + 1] = food
  else
    changed[#changed + 1] = body[#body]
    table.remove(body)
  end

  return true
end

-- ---------------------------------------------------------------- the file

-- **One line per thing**, the same shape as the save of every app (docs/45): the game first and then the numbers, and a file
-- of another field —another build, another game— is a new game and not a page with the wrong worm on it.
local function save()
  -- **Una demostració no es desa**: the worm that the CPU is playing is not the game a person left behind, and a save of it would
  -- come back as a game nobody played (docs/30, docs/45).
  if demo then return false end

  local cells = {}
  for i = 1, #body do cells[i] = tostring(body[i]) end

  local lines = {
    tostring(Cols),
    tostring(Rows),
    table.concat(cells, ' '),
    tostring(food),
    tostring(dirX),
    tostring(dirY),
    tostring(score),
    tostring(level),
    tostring(levelBase),
    (over and '1' or '0'),
    why,
  }

  local ground = fs.data()
  if ground ~= nil then pcall(fs.mkdir, ground) end

  local written, reason = fs.write(fs.data(FileName), table.concat(lines, '\n'))
  if written == nil then
    local said = (type(reason) == 'table') and (reason.code or reason.message) or reason
    console.print('[snake] could not save: ' .. tostring(said))

    return false
  end

  return true
end

local function load()
  local text = fs.read(fs.data(FileName))
  if text == nil then return false end

  local lines = {}
  for line in (text .. '\n'):gmatch('([^\n]*)\n') do lines[#lines + 1] = line end
  if #lines < 11 then return false end
  if tonumber(lines[1]) ~= Cols or tonumber(lines[2]) ~= Rows then return false end

  body = {}
  for piece in lines[3]:gmatch('%d+') do
    local at = tonumber(piece)
    if (at == nil) or (at < 1) or (at > Cols * Rows) then return false end
    body[#body + 1] = at
  end
  if #body < 1 then return false end

  food = tonumber(lines[4]) or 0
  dirX = tonumber(lines[5]) or 1
  dirY = tonumber(lines[6]) or 0
  score = tonumber(lines[7]) or 0
  level = tonumber(lines[8]) or 0
  levelBase = tonumber(lines[9]) or 0
  over = (lines[10] == '1')
  why = lines[11]
  wantX, wantY = nil, nil

  return true
end

local function loadBest()
  local text = fs.read(fs.data(BestName))
  if text == nil then return end

  local number = tonumber((text:gsub('%s+', '')))
  if number ~= nil then best = number end
end

-- ---------------------------------------------------------------- the drawing

local function cellX(col) return fieldX + (col - 1) * cell end
local function cellY(row) return fieldY + (row - 1) * cell end

-- **A cell of the worm is ink and a cell of paper is paper**, and nothing else: what is in a cell is told by its size —the
-- worm fills it, the food does not— and by nothing else (docs/30, docs/92).
local function paintCell(i, mode)
  local x = cellX(colOf(i))
  local y = cellY(rowOf(i))

  ui.rect(x, y, cell, cell, Paper, mode)

  for k = 1, #body do
    if body[k] == i then
      ui.rect(x, y, cell, cell, Ink, mode)
      return
    end
  end

  if i == food then
    -- **A piece of food is smaller than a cell**, so what fills a whole cell is always the worm.
    local side = math.max(4, cell - 8)
    ui.rect(x + math.floor((cell - side) / 2), y + math.floor((cell - side) / 2), side, side, Ink, mode)
  end
end

-- **And the segments are seen**: one pixel of paper between two cells of the worm that touch, and only between them —a white
-- line next to a cell of paper is a line that changes nothing and costs a row (docs/45).
local function paintSegments(mode)
  for i = 2, #body do
    local a, b = body[i - 1], body[i]
    local ra, ca = rowOf(a), colOf(a)
    local rb, cb = rowOf(b), colOf(b)

    if (ra == rb) and (math.abs(ca - cb) == 1) then
      ui.rect(cellX(math.max(ca, cb)), cellY(ra), 1, cell, Paper, mode)
    elseif (ca == cb) and (math.abs(ra - rb) == 1) then
      ui.rect(cellX(ca), cellY(math.max(ra, rb)), cell, 1, Paper, mode)
    end
  end
end

-- **The field is the wall and its paper**: the wall is painted first and the paper over it, so the wall is a band of a known
-- width around a field that is exactly `fieldW` by `fieldW` (the old app's own rule).
local function paintField(mode)
  ui.rect(fieldX - WallPx, fieldY - WallPx, fieldW + 2 * WallPx, fieldW + 2 * WallPx, Wall, mode)
  ui.rect(fieldX, fieldY, fieldW, fieldW, Paper, mode)
end

-- **The worm and the food, all of them**: what the page does once and what a step does to the cells it touched, with the
-- same two functions and not two drawings that can drift apart (docs/30).
local function paintWorm(mode)
  -- **El camp ja és paper** (`paintField`) i el que hi ha a sobre és el cuc i el menjar: una cel·la del cuc és tinta sencera i
  -- el menjar és més petit que una cel·la, i no cal preguntar-ho cel·la a cel·la en pintar la pàgina (docs/30).
  for i = 1, #body do
    ui.rect(cellX(colOf(body[i])), cellY(rowOf(body[i])), cell, cell, Ink, mode)
  end

  paintSegments(mode)
  if food > 0 then paintCell(food, mode) end
end

-- **The words of the middle, when the game is stopped**: with no card of the system, the page says it itself —a game that
-- is over is not paused, and a game that is being played says nothing (the old app's band, painted by this file).
local function paintBand(mode)
  if (not over) and (not paused) then return end

  local words = over and 'GAME OVER' or 'PAUSED'
  local scale = kBody
  local tw = ui.text_width(words, scale)
  local lh = ui.line_height(scale)
  local padX, padY = 16, 12
  local boxW = tw + 2 * padX
  local boxH = lh + 2 * padY
  local x = fieldX + math.floor((fieldW - boxW) / 2)
  local y = fieldY + math.floor((fieldW - boxH) / 2)

  ui.rect(x, y, boxW, boxH, Paper, mode)
  ui.frame(x, y, boxW, boxH, Ink, mode)
  ui.text(x + padX, y + padY, words, scale, Ink, mode)
end

-- **The strip: the score, the level, its bar and the speed**, which is what a person reads while playing. The old app's own
-- line and its own bar, with the two doors this port has instead of its card.
local function speedText() return string.format('%d ms', stepMs()) end

local function scoreText()
  -- **I de qui és la partida es diu aquí**: `L 3` és el nivell de la persona i `CPU 3` el de la demostració, que són dues escales
  -- i no una (the old app's `paintStatus` and its `tag`, docs/45).
  return string.format('Score %d   Best %d   %s %d', score, best, demo and 'CPU' or 'L', demo and cpuLevel or level)
end

local function paintStatus(mode)
  if the_score ~= nil then the_score.text = scoreText() end
  if the_speed ~= nil then the_speed.text = speedText() end

  -- **The bar of the level is a frame and its ink**: a bar of no progress is a frame with nothing in it, and a bar of the
  -- whole level is a frame full of ink (the old app's `paintStatus`). **I és tan ampla com la columna del marcador** i no com
  -- la pàgina: els dos botons són a la dreta i una barra que els travessa és una barra que tapa el que es pot tocar.
  local barW = scoreW
  ui.rect(Margin, barY, barW, BarH, Paper, mode)
  ui.frame(Margin, barY, barW, BarH, Ink, mode)

  local inside = barW - 2 * BarBorder
  local got = math.floor(inside * levelProgress())
  if got > 0 then ui.rect(Margin + BarBorder, barY + BarBorder, got, BarH - 2 * BarBorder, Ink, mode) end
end

-- **What the line of the bottom says, and one thing at a time**: a game that is over is not paused, and a game nobody has
-- touched says what a finger does.
local function footerText()
  if demo then return 'The CPU is playing. Tap to take over.' end
  if over then
    if why == 'wall' then return 'The wall. Tap New.' end
    if why == 'tail' then return 'Its own tail. Tap New.' end
    if why == 'full' then return 'The field is full - you win!' end

    return 'Game over. Tap New.'
  end
  if paused then return 'Paused. Tap Resume.' end
  if score == 0 then return 'Tap where you want the head to go.' end

  return 'Tap to turn.'
end

local function paintFooter(mode)
  if the_foot ~= nil then the_foot.text = footerText() end

  if the_pause ~= nil then the_pause.title = paused and 'Resume' or 'Pause' end
end

-- **The ink of one box is waited before the next is asked for** (`ui.idle`, docs/30): a box that arrives while the glass
-- still moves the one before is refused and stays owed, and a queue of refused boxes is a page painted in one pass.
local function waitForTheInk()
  for _ = 1, 24 do
    if ui.idle() then return end
    sys.delay(40):await()
  end
end

-- **THE PAGE IS BUILT FROM SCRATCH IN ONE PLACE**: the paper, the field, the worm, the strip and the line, and the system
-- asks every control it covered to draw again when the paper is laid (docs/30, docs/50).
local function paintPage()
  ui.clear(15)
  ui.rect(0, 0, W, H, 15, kPageMode)
  paintField(kPageMode)
  paintWorm(kPageMode)
  paintBand(kPageMode)
  paintStatus(kPageMode)
  paintFooter(kPageMode)
  ui.present(kPageMode)
  waitForTheInk()

  lastScore, lastLevel, lastFoot = score, level, footerText()
end

-- **What a step changed, and nothing else**: two to four boxes and one present of four greys.
local function paintChanged()
  if #changed == 0 then return end

  local done = {}
  for _, i in ipairs(changed) do
    if not done[i] then
      done[i] = true
      paintCell(i, kStepMode)
    end
  end

  -- **And the segments of the cells that have arrived**: a new cell of the worm that touches the one before it has to lose
  -- the pixel that makes it two cells and not a blot.
  paintSegments(kStepMode)

  changed = {}
  ui.present(kStepMode)
  waitForTheInk()
end

-- ---------------------------------------------------------------- the geometry

local function measure(w, h)
  -- **L'alçada de la franja es mesura i no s'endevina** (docs/30, docs/45): dues files de text —el marcador i el pas del joc—,
  -- la barra del nivell i l'aire de tot plegat. Una franja d'un número fix és una fila de text que tapa el camp a la primera
  -- cara que no és la d'aquesta placa.
  local lineS = ui.line_height(kSmall)
  stripH = 6 + 2 * lineS + 2 + 8 + BarH + 6
  if stripH < Strip then stripH = Strip end

  local availW = w - 2 * Margin
  local availH = h - stripH - Footer
  local side = (availW < availH) and availW or availH

  cell = math.floor((side - 2 * WallPx) / Cols)
  if cell < 8 then cell = 8 end
  fieldW = cell * Cols
  fieldX = math.floor((w - fieldW) / 2)
  fieldY = stripH + math.max(0, math.floor((h - stripH - Footer - fieldW) / 2))

  -- **El botó es mesura de la seva paraula** (docs/30, docs/45): a pixel of difference is a word with dots at the end.
  newW = ui.text_width('New', kSmall) + 2 * BtnPad + 2
  if newW < BtnMinW then newW = BtnMinW end
  newX = w - Margin - newW
  newY = 6

  pauseW = ui.text_width('Resume', kSmall) + 2 * BtnPad + 2
  if pauseW < BtnMinW then pauseW = BtnMinW end
  pauseX = newX - 8 - pauseW
  if pauseX < Margin then pauseX = Margin end
  pauseY = 6

  -- **I la tercera porta: la demostració** (the old app's `DEMO` of its card, which here is a button because this page has no
  -- card, docs/45). El botó diu `Demo` i prou: el que fa és començar una partida que juga el CPU, i un toc al camp la torna.
  demoW = ui.text_width('Demo', kSmall) + 2 * BtnPad + 2
  if demoW < BtnMinW then demoW = BtnMinW end
  demoX = pauseX - 8 - demoW
  if demoX < Margin then demoX = Margin end

  if the_new == nil then
    the_new = ui.button(newW, BtnH)
    if the_new ~= nil then
      the_new.focusable = false
      the_new.scale = kSmall
      the_new.title = 'New'
    end
  else
    the_new:resize(newW, BtnH)
  end
  if the_new ~= nil then the_new:place(newX, newY) end

  if the_pause == nil then
    the_pause = ui.button(pauseW, BtnH)
    if the_pause ~= nil then
      the_pause.focusable = false
      the_pause.scale = kSmall
    end
  else
    the_pause:resize(pauseW, BtnH)
  end
  if the_pause ~= nil then the_pause:place(pauseX, pauseY) end

  if the_demo == nil then
    the_demo = ui.button(demoW, BtnH)
    if the_demo ~= nil then
      the_demo.focusable = false
      the_demo.scale = kSmall
      the_demo.title = 'Demo'
    end
  else
    the_demo:resize(demoW, BtnH)
  end
  if the_demo ~= nil then the_demo:place(demoX, pauseY) end

  -- **La línia del marcador té el que deixen els dos botons**, i la de baix tota l'amplada de la pàgina.
  scoreW = demoX - Margin - 8
  if scoreW < 40 then scoreW = 40 end
  barY = stripH - 6 - BarH

  if the_score == nil then
    the_score = ui.label(scoreW, lineS)
    if the_score ~= nil then
      the_score.scale = kSmall
      the_score.align = 'left'
      the_score.valign = 'middle'
    end
  else
    the_score:resize(scoreW, lineS)
  end
  if the_score ~= nil then the_score:place(Margin, 6) end

  -- **La velocitat és a sota del marcador i no al costat**, perquè el número que diu el pas del joc és el que explica la barra
  -- de sobre (docs/30).
  local speedW = scoreW
  if the_speed == nil then
    the_speed = ui.label(speedW, lineS)
    if the_speed ~= nil then
      the_speed.scale = kSmall
      the_speed.align = 'left'
      the_speed.valign = 'middle'
    end
  else
    the_speed:resize(speedW, lineS)
  end
  if the_speed ~= nil then the_speed:place(Margin, 6 + lineS + 2) end

  local footW = w - 2 * Margin
  if the_foot == nil then
    the_foot = ui.label(footW, 30)
    if the_foot ~= nil then
      the_foot.scale = kSmall
      the_foot.align = 'left'
    end
  else
    the_foot:resize(footW, 30)
  end
  if the_foot ~= nil then the_foot:place(Margin, fieldY + fieldW + 8) end
end

-- ---------------------------------------------------------------- the finger

-- **A tap is a direction and not a cell**: where the finger lands says which way the head has to go from where it is, the axis
-- with more travel decides, and a tap that points straight back is read as the sideways half of it —a finger that means
-- «turn» and pushes back is not a finger asking for a wall (docs/30).
local function dirName(ax, ay)
  if ax > 0 then return 'right' end
  if ax < 0 then return 'left' end
  if ay < 0 then return 'up' end

  return 'down'
end

-- **The turn it has taken is said**, which is what a test reads without playing the game (the 2048 says its board on every
-- move for the same reason, docs/45): one line per finger and never one per step, or a game that steps twice a second would
-- be a log nobody can read.
local function sayTurn()
  console.print(string.format('[snake] turn=%s len=%d score=%d level=%d step=%d over=%d paused=%d demo=%d why=%s',
    dirName(wantX or 0, wantY or 0), #body, score, demo and cpuLevel or level, stepMs(), over and 1 or 0, paused and 1 or 0,
    demo and 1 or 0, (why == '') and '-' or why))
end

local function turnToward(x, y)
  if over then return false end
  -- **Un toc al camp durant una demostració la pren**: the old app's rule was «any key of the person hands it over», and here the
  -- key is the finger —a demo that could not be taken back would be a page a person has to leave to stop (docs/30, docs/45).
  if demo and not paused then
    demo = false
    save_state()
  end
  if paused then return false end

  local headRow, headCol = rowOf(body[1]), colOf(body[1])
  local cx = cellX(headCol) + math.floor(cell / 2)
  local cy = cellY(headRow) + math.floor(cell / 2)
  local dx, dy = x - cx, y - cy

  if (math.abs(dx) < kTapPx) and (math.abs(dy) < kTapPx) then return false end

  local function fits(ax, ay) return not ((ax == -dirX) and (ay == -dirY)) end

  local horizontal = (math.abs(dx) >= math.abs(dy))
  local ax = horizontal and ((dx > 0) and 1 or -1) or 0
  local ay = horizontal and 0 or ((dy > 0) and 1 or -1)

  if fits(ax, ay) then
    wantX, wantY = ax, ay
    sayTurn()
    return true
  end

  -- **The tap points back**: the half of it that is not the way back is the turn it meant, when it has one.
  if (ax ~= 0) and (dy ~= 0) then
    if fits(0, (dy > 0) and 1 or -1) then
      wantX, wantY = 0, (dy > 0) and 1 or -1
      sayTurn()
      return true
    end
  elseif (ay ~= 0) and (dx ~= 0) then
    if fits((dx > 0) and 1 or -1, 0) then
      wantX, wantY = (dx > 0) and 1 or -1, 0
      sayTurn()
      return true
    end
  end

  return false
end

-- **Els dos botons es miren aquí i no al sistema** (they are `focusable = false`, so the finger arrives at this file), which is
-- what leaves the centre of the dial to the system —the three presses that close an app (docs/30).
local function tapAt(x, y)
  if (x >= newX) and (x < newX + newW) and (y >= newY) and (y < newY + BtnH) then
    -- **A new game runs**: a page that comes back stopped and a `New` that does not start it is a page that has to be told
    -- twice (docs/30).
    paused = false
    demo = false
    newGame()
    save()
    console.print(stateLine())
    save_state()
    paintPage()

    return true
  end

  if (x >= demoX) and (x < demoX + demoW) and (y >= pauseY) and (y < pauseY + BtnH) then
    -- **La demostració comença on és la persona**: the CPU's own ladder starts at the level of the game that was being played and
    -- climbs on its own from there, which is what makes a demo worth watching (docs/45).
    newGame()
    demo = true
    cpuLevel = 0
    cpuPointsBase = 0
    paused = false
    paintPage()
    console.print(stateLine())

    return true
  end

  if (x >= pauseX) and (x < pauseX + pauseW) and (y >= pauseY) and (y < pauseY + BtnH) then
    if over then return false end

    paused = not paused
    save_state()
    paintPage()

    return true
  end

  return turnToward(x, y)
end

-- ---------------------------------------------------------------- the app

local caps = sys.caps()
if (caps == nil) or not caps.display then
  console.print('snake: this board has no glass')
  return
end

W, H = ui.caps().width, ui.caps().height

-- **La forma de la vista, abans de col·locar res** (docs/30): whether the page was stopped is read here and the page is built
-- with it, and a block of a shape this build does not know is thrown away.
local shape = load_state()
if (shape ~= nil) and (shape.paused ~= nil) then paused = (shape.paused == '1') end

-- **I el joc torna on era**, i una placa sense partida desada és una partida nova —no un error— (docs/45).
loadBest()
if not load() then newGame() end

measure(W, H)

-- **`run snake keys` diu la geometria** so a test asks instead of guessing a corner, and `seed=N` fixes the machine's numbers,
-- which is what a test that reads the cell of the food needs (docs/45).
local asked = (app.args ~= nil) and tostring(app.args.path or '') or ''

local seed = asked:match('seed=(%d+)')
if (seed ~= nil) and (math.randomseed ~= nil) then math.randomseed(tonumber(seed)) end

if asked == 'keys' then
  console.print(string.format('[snake] cell %d, field %d,%d %dx%d, new %d,%d %dx%d, pause %d,%d %dx%d, demo %d,%d %dx%d', cell,
    fieldX, fieldY, fieldW, fieldW, newX, newY, newW, BtnH, pauseX, pauseY, pauseW, BtnH, demoX, pauseY, demoW, BtnH))
end

-- **`full` és una partida en un estat conegut**: a worm of six cells going right, the food four cells in front of it and a
-- score of four, which is one under the pieces the first level asks for —a page to read without playing a game, and the only
-- way a test can see the worm, the food and the strip together (docs/45).
if asked == 'full' then
  newGame()
  body = {}
  local row = math.floor(Rows / 2)
  local col = 6                    -- the head here, and the six cells go to its left: the worm is at home in the field
  for i = 1, 6 do body[i] = idx(row, col - (i - 1)) end
  dirX, dirY = 1, 0
  score, level, levelBase = 4, 0, 0
  over, why = false, ''
  food = idx(row, col + 4)
  paused = false
end

-- **`demo` és el CPU jugant**, que és l'única manera que una prova el vegi: the CPU chooses every direction of a game with a random
-- food, and a case that read a person's game would be measuring the dice (docs/45).
if asked == 'demo' then
  newGame()
  demo = true
  cpuLevel, cpuPointsBase = 0, 0
  paused = false
end

-- **The state line**: what the game is, in one line and always under the sixty-four characters a notice carries, so a test
-- reads it instead of guessing from a picture (docs/45).
function stateLine()
  return string.format('[snake] len=%d score=%d level=%d step=%d over=%d paused=%d demo=%d why=%s', #body, score,
    demo and cpuLevel or level, stepMs(), over and 1 or 0, paused and 1 or 0, demo and 1 or 0, (why == '') and '-' or why)
end

paintPage()
console.print(stateLine())

function on_pause() save(); save_state() end
function on_save() save(); save_state() end
function on_close() save(); save_state() end

-- **The clock of the game is a date and not the wait of the turn**: the step is due at `nextStep` and the loop looks at the
-- finger every sixteen milliseconds, so a tap lands while the worm is between two cells and does not wait for the step to
-- come (docs/30). **And what the glass takes to paint is not taken off the pace**: the present of a step is 120 ms and the
-- step of level zero is 220, so the next step is counted from the last one and not from the end of the paint.
local nextStep = sys.time_ms() + stepMs()

-- **El banc diu el que fa a cada pas** (the old app's `-probe`): a test of the CPU has to read the score of a game that lasts,
-- and this one lasts longer than the clock of a case (docs/45).
local bench = (asked == 'demo')

while true do
  local view = ui.caps()
  if (view.width ~= W) or (view.height ~= H) then
    W, H = view.width, view.height
    measure(W, H)
    paintPage()
  end

  -- **Els gestos es prenen i la cua es buida**: a gesture nobody takes is a queue that grows, and the finger of this page is
  -- a tap (and a drag so short it is a tap that moved) (docs/30).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'tap' then
      tapAt(gesture.x or 0, gesture.y or 0)
    elseif gesture.kind == 'drag' then
      if (math.abs(gesture.dx or 0) < kTapPx) and (math.abs(gesture.dy or 0) < kTapPx) then
        tapAt(gesture.x or 0, gesture.y or 0)
      end
    end

    gesture = touch.gesture()
  end

  -- **The strip, when it says something else**: the score, the level and the bar are one box and one pass, and only when a
  -- piece of food has moved them.
  if (score ~= lastScore) or (level ~= lastLevel) then
    paintStatus(kTextMode)
    ui.present(kTextMode)
    waitForTheInk()
    lastScore, lastLevel = score, level
  end

  -- **I la línia de baix, quan diu una altra cosa**: one box and one pass.
  local foot = footerText()
  if foot ~= lastFoot then
    paintFooter(kTextMode)
    ui.present(kTextMode)
    waitForTheInk()
    lastFoot = foot
  end

  -- **The step**, when it is due and the game is going.
  if (not over) and (not paused) then
    local now = sys.time_ms()
    if now >= nextStep then
      if step() then
        paintChanged()
        -- **Una demostració diu el que fa a cada pas**: `demo` és un banc i no una partida d'una persona —una prova que llegís
        -- només el començament mesuraria el rellotge i no el CPU, i aquest CPU aguanta molt més que el rellotge d'un cas— (the
        -- old app's `-probe`, docs/45).
        if bench then console.print(stateLine()) end
      else
        -- **A game that ends is desada i la pàgina ho diu**: the cells of the last step first —a worm that ate the last free
        -- cell has moved even if the game is over—, and then the words of the middle and the line of the bottom, in the quality
        -- of text, and no page: a gl16 for a game over is a blink nobody asked for (docs/50).
        paintChanged()
        save()
        console.print(stateLine())
        paintBand(kTextMode)
        paintFooter(kTextMode)
        ui.present(kTextMode)
        waitForTheInk()
        lastFoot = footerText()
      end

      -- **El compàs és el de la llista i no el del quadre**: the next step counts from this one, and a paint that took longer
      -- than a step does not make the worm jump two cells —the panel cannot go faster than a present, and that is said here
      -- instead of being discovered by a person losing a game they could not see (docs/50, docs/95).
      nextStep = nextStep + stepMs()
      local after = sys.time_ms()
      if (nextStep <= after) or ((nextStep - after) > 4 * stepMs()) then nextStep = after + stepMs() end
    end
  else
    nextStep = sys.time_ms() + stepMs()
  end

  sys.delay(16):await()
end
