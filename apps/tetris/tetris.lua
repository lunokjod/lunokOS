-- tetris: the well of ten by eighteen, in black and white.
--
-- **IT IS THE TETRIS OF THE GAME BOY OF 1989** (Nintendo), and of it this port keeps the four things the machine wrote
-- down: the playfield of ten by eighteen, the pieces that enter with their highest block on the top row, the rotation
-- system that **does not move a piece to make a turn fit** —no wall kick and no lock delay— and the scoring of the original,
-- forty, a hundred, three hundred and twelve hundred for one, two, three and four rows multiplied by the level plus one.
-- The pace of the fall is the table of the original in frames, derived to milliseconds, so the ladder is one table and not
-- two numbers that can disagree (the old project's own rule, docs/45).
--
-- **AND EVERYTHING IS INK OR PAPER** (the owner, 22/11: *«Fixa't que el tauler dibuixa el menys possible i tot és blanc i
-- negre»*). One drawing for the whole game —a cell is a square of ink with a pixel of paper on two of its sides— for the
-- piece in the air, for the pile and for the box of the next one; what tells a tetromino from another is its shape, which
-- is what is left when the drawing is one. And because a cell of the game and a cell of the pile are **the same drawing**,
-- **a piece that lands costs NOTHING to paint**: the cells it has just settled on were already the cells the pile has (the
-- old app had to repaint them because its piece was an outline and its pile was filled, and it paid for it with half-drawn
-- cells, 12/10, docs/45). What is painted is what moves: the four cells the piece leaves and the four where it arrives, in
-- **one** present of `du` —two tones, 120 ms, no flash—.
--
-- **AND THERE IS NO GAMEPAD** (the owner: *«No facis el gamePad. Usarem els dits per girar la peça (polsant a sobre) o
-- moure-la a esquerra, dreta o avall»*). The old app steered with the cross, `A` and `B`; this one has the well and the
-- strip: **a tap on the piece turns it**, a tap to its left or its right moves it one column that way, and a tap below it
-- drops it one row. A tap above it turns it too, because up is not a way a piece goes. **I un toc que no cap no passa**: el
-- joc de l'original no mou la peça per fer-la girar, i un dit que demana un gir impossible no rep res.
--
-- **THE REDRAW IS THE APP.** Three policies and none of them is «paint the page»:
--   - **a move, a turn or a row** paints the cells that changed and nothing else —at most eight boxes in one present of `du`—;
--   - **the strip** —score, level, lines and the bar of the level— is a box of its own in `gld16`, only when a number moves;
--   - **the page** is painted once: when the app opens, when the view changes its size and when a game begins.
--
-- **I UNA LÍNIA PLENA FA BLINK I DESAPAREIX** (the owner, 16/10: *«en cas de col·lapsar una linia: fas blink i fas
-- desapareixer la linia»*): two passes of the ink —the row goes to paper, comes back, goes to paper— of 120 ms each and the
-- pile above comes down in the next pass, without an effect of its own.
--
-- **I HI HAVIA UNA COSA DEL VELL QUE AQUEST PORT S'HAVIA DEIXAT I HAVIA DE TORNAR** (0.190.256, l'amo: *«Les peces perden la
-- forma quan giren!!! revisa bé el codi original»*): **el rastre i la neteja**. `du` pinta i **no neteja**, i les cel·les que una
-- peça deixa es queden amb la seva marca: en un joc on la peça gira i cau sobre les mateixes quatre cel·les el residu s'acumula i el
-- que es veu és **una còpia de la figura** —el fantasma que el vell va mesurar, 2,0 de residu per fila contra 0,6 de `gld16` (docs/45)—.
-- El nucli no ho escombra: el que fa és comptar les passades i pujar a `gc16` una `du` que cobreixi gairebé tot el vidre (docs/50), i
-- això no és el mateix. O sigui que el port apunta el rectangle que la peça ha recorregut i **en demana una passada que neteja cada
-- `TrailRows` moviments**, que és el que feia l'original amb els seus dotze.
--
-- **THE GAME IS DATA AND THE PAUSE IS FORM**: the well, the score, the lines, the level and the next piece live in
-- `fs.data('game.txt')` and come back where they were; whether the page is stopped goes to the `state` block, because a
-- pause is the shape of the view a person left behind (docs/30, docs/45 §5 bis). **The demo is nobody's game**: a
-- demonstration never writes the file nor the level, so a board that played itself for a while does not open at level twelve.

local Cols, Rows = 10, 18           -- the playfield of the original (tetris.wiki, *Tetris (Game Boy)*: "Playfield size 10 x 18")

-- **The seven pieces and their states, in a box of four by four** —the size of the `I`, which is the longest one—. A state is
-- sixteen cells written as they are looked at, and a turn only moves cells INSIDE the box: the box never turns, which is what
-- makes a rotation a table lookup and not arithmetic (docs/45).
local Shapes = {
  I = { {0,0,0,0, 1,1,1,1, 0,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,0,1,0, 0,0,1,0, 0,0,1,0},
        {0,0,0,0, 0,0,0,0, 1,1,1,1, 0,0,0,0},
        {0,1,0,0, 0,1,0,0, 0,1,0,0, 0,1,0,0} },

  O = { {0,1,1,0, 0,1,1,0, 0,0,0,0, 0,0,0,0} },

  S = { {0,1,1,0, 1,1,0,0, 0,0,0,0, 0,0,0,0},
        {0,1,0,0, 0,1,1,0, 0,0,1,0, 0,0,0,0} },

  T = { {0,1,0,0, 1,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,0,0, 0,1,1,0, 0,1,0,0, 0,0,0,0},
        {0,0,0,0, 1,1,1,0, 0,1,0,0, 0,0,0,0},
        {0,1,0,0, 1,1,0,0, 0,1,0,0, 0,0,0,0} },

  Z = { {1,1,0,0, 0,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,0,1,0, 0,1,1,0, 0,1,0,0, 0,0,0,0} },

  J = { {1,0,0,0, 1,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,1,0, 0,1,0,0, 0,1,0,0, 0,0,0,0},
        {0,0,0,0, 1,1,1,0, 0,0,1,0, 0,0,0,0},
        {0,1,0,0, 0,1,0,0, 1,1,0,0, 0,0,0,0} },

  L = { {0,0,1,0, 1,1,1,0, 0,0,0,0, 0,0,0,0},
        {0,1,0,0, 0,1,0,0, 0,1,1,0, 0,0,0,0},
        {0,0,0,0, 1,1,1,0, 1,0,0,0, 0,0,0,0},
        {1,1,0,0, 0,1,0,0, 0,1,0,0, 0,0,0,0} },
}

local Pieces = { "I", "O", "T", "S", "Z", "J", "L" }

-- **The scoring of the original**: one, two, three and four rows at once, times the level plus one.
local LineScore = { 40, 100, 300, 1200 }

-- **How long a row takes to fall, in frames of the machine of 1989** (59,73 per second): 53, 49, 45, 41, 37, 33, 28, 22, 17,
-- 11, 10, 9, 8, 7, 6, 6, 5, 5, 4, 4 and 3 for the levels zero to twenty. Written in frames and derived, because a table
-- copied twice is a table that one day says two things (docs/45).
local Fps = 59.73
local RowFrames = { 53, 49, 45, 41, 37, 33, 28, 22, 17, 11, 10, 9, 8, 7, 6, 6, 5, 5, 4, 4, 3 }

-- **Two, i el terra és del vidre i no del joc**: a present of `du` costs 120 ms, and a row faster than a pass is a row the
-- panel cannot paint; the two hundred leave 120 for the pass and 80 for the turn of the loop (the old app's own two numbers,
-- docs/45, docs/95).
local DropScale = 2
local RowFloorMs = 200

-- **What a level asks for**: points, `500 x (level + 1)`, so the bar of the strip and the level are the same number and
-- neither of them can lie — twelve single rows, five doubles, one tetris and a bit at any step of the ladder— (the owner,
-- 12/10, docs/45).
local PointsPerLevel = 500
local LevelMax = 20

-- **Les dues tintes del joc** and nothing else: the ink and the paper of the mode `du`, which carries two tones and is the
-- fastest waveform of this glass (120 ms, no flash). No grey anywhere: a two-tone game is a game that travels whole.
local Ink, Paper = 0, 15
local Gap = 1                     -- a pixel of paper on two sides of a cell, which is what makes a cell a square
local kPageMode = 'du'
local kStepMode = 'du'
local kTextMode = 'gld16'         -- text: the greys of a letter have to land on themselves, and it cleans (docs/50)
-- **La passada que neteja**: `du` pinta i no neteja, i el residu d'una peça que gira i cau sobre les mateixes cel·les és una còpia de
-- la seva figura. `gld16` és la més barata que neteja —450 ms— i és la que el vell va mesurar per a això (docs/45, docs/50).
local kCleanMode = 'gld16'
local TrailRows = 12              -- moviments entre dues neteixes: el número de l'original, i el que mana és el parpelleig

local Strip = 56                  -- the strip of the numbers, above the well
local Footer = 44                 -- the line of the bottom
local Margin = 12                 -- the paper on the sides
local Edge = 3                    -- the ink around the well, in pixels
local BtnH = 63
local BtnPad = 16
local BtnMinW = 84
local BarH = 6
local BarBorder = 1
local FileName = 'game.txt'
local kTapPx = 12                 -- a tap this close to the middle of the piece is a tap on it
local TapSlop = 12                -- and a drag this short is a tap that moved, which is the finger of this glass
local NextReserve = 92            -- the column of the next piece, kept off the well so the well does not have to be narrow

-- **The CPU of the demo**: the cadence comes out of the pace of the fall —a row of the level is worth eight steps and never
-- fewer than the floor—, and the weights are the classic evaluator (a finished row is worth a lot, a hole is a debt, a tall
-- stack a bigger one and the raggedness of the top the smallest of the three). They are the old app's numbers, and with the
-- signs flipped it fills the well in twenty pieces, which is what a broken evaluator looks like (docs/45).
local CpuStepsPerRow = 8
local CpuFloorMs = 90
local CpuMaxMs = 320
local EvalLine, EvalHeight, EvalHole, EvalBumpy = 0.76, -0.51, -0.36, -0.19

-- **The face of the paper, asked for by size and not by step** (docs/45): small 33 pixels of line and body 43, so the page
-- keeps the same physical size as the other apps (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  return math.max(1, math.min(16, math.floor(line_px / step + 0.5)))
end

local kSmall = scale_for(33)
local kBody = scale_for(43)

-- ---------------------------------------------------------------- what two functions share

-- **Tot el que dues funcions comparteixen es declara aquí** (the house's own rule, docs/45): a `local` written after a
-- function is a global inside it, and this file learned it the way every Lua file does.

local state = nil                 -- the `state` block
local paused = false              -- the shape of the view
local over = false                -- the well is full and there is no piece

local well = {}                   -- Rows x Cols: a cell is `true` when the pile is there
local piece = nil                 -- `{ name, rot, x, y }` in board coordinates, 1-based
local nextName = 'I'              -- the piece of the box
local lastPiece = nil             -- the promise that no piece comes twice in a row
local score, lines, level = 0, 0, 0
local levelBase = 0               -- the score the level in play began with

local demo = false                -- the CPU is playing this game
local cpuRot, cpuX = nil, nil     -- where it wants the piece
local cpuLevel, cpuBase = 0, 0    -- the CPU's own ladder, which is not the person's

local cell = 0                    -- one cell of the well, in pixels
local wellX, wellY, wellW, wellH = 0, 0, 0, 0
local stripH = Strip
local W, H = 0, 0
local barY, barW = 0, 0
local nextBoxX, nextBoxY, nextBoxW, nextBoxH = 0, 0, 0, 0

local the_score, the_level, the_bar, the_foot, the_pause, the_new, the_demo
local pauseX, pauseY, pauseW, newX, newY, newW, demoX, demoW
local lastScore, lastLevel, lastFoot = -1, -1, ''

-- **The cells that have to be painted on this present**: what the piece has left and where it is now, which is at most
-- eight boxes. `changed` is filled by whoever moves the piece and emptied by `paintChanged`.
local changed = {}
local changedN = 0

-- **El rastre**: the rectangle the piece has swept since the last clean, in cells, and how many movements that is. It is what
-- `cleanTheTrail` asks the glass to clean, and it lives here because the ghost is the glass's and not the canvas's (docs/45).
local trailLeft, trailTop, trailRight, trailBottom = 0, 0, 0, 0
local trailOpen, trailMoves = false, 0

local function mark(col, row, tone, mode)
  if col < 1 or col > Cols or row < 1 or row > Rows then return end

  changedN = changedN + 1
  changed[changedN] = { col = col, row = row, tone = tone, mode = mode }
end

-- **El que es declara amunt i s'escriu avall** (docs/45): `tapAt` ha de poder dir la línia de l'estat, `paintChanged` ha
-- d'esperar la tinta, i la pàgina la demanen els botons, que es llegeixen abans que ella. Una funció que es declara després del
-- seu lector és un global dins d'ell, i aquest fitxer ho va aprendre com tots els de Lua.
local stateLine
local waitForTheInk
local paintPage
local noteTheTrail

-- ---------------------------------------------------------------- the state of the page (form)

local kStateBytes = 512
local kLayout = 'v1'

local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end

  return state
end

-- **What the page is now**: whether it is stopped, and nothing else —the game is data and lives in `fs.data`— (docs/30).
local function save_state()
  local block = the_state()
  if block == nil then return false end

  block:clear()
  block:write(0, kLayout .. '\npaused=' .. (paused and '1' or '0'))

  return true
end

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

-- ---------------------------------------------------------------- the well

local function idx(col, row) return (row - 1) * Cols + col end
local function cellAt(col, row)
  if col < 1 or col > Cols or row < 1 or row > Rows then return nil end
  return well[idx(col, row)]
end
local function setCell(col, row, there) well[idx(col, row)] = there end

-- **Where the cells of a piece are, in board coordinates**, for one of its states (the old app's `cellsOf`, docs/45).
local function cellsOf(name, rot, x, y)
  local shape = Shapes[name][rot]
  local out = {}
  local n = 0

  for r = 0, 3 do
    for c = 0, 3 do
      if shape[r * 4 + c + 1] == 1 then
        n = n + 1
        out[n] = { x + c, y + r }
      end
    end
  end

  return out
end

-- **The first row of the box that has anything in it**: a piece is born with the top of its SHAPE on the first row of the
-- well, and the box has empty rows above the shape —the `I` has exactly one—, so spawning every piece at the same `y` would
-- start the `I` a row lower than the rest (the old app's `topRowOf`, docs/45).
local function topRowOf(name)
  local shape = Shapes[name][1]

  for r = 0, 3 do
    for c = 0, 3 do
      if shape[r * 4 + c + 1] == 1 then return r end
    end
  end

  return 0
end

-- **Can the piece be there?** The walls and the settled cells are the same question, and a cell above the well is air.
local function fits(name, rot, x, y)
  local cells = cellsOf(name, rot, x, y)

  for i = 1, #cells do
    local c, r = cells[i][1], cells[i][2]

    if c < 1 or c > Cols or r > Rows then return false end
    if r >= 1 and cellAt(c, r) then return false end
  end

  return true
end

-- **The pieces do not come out of a bag**: the machine of 1989 asked the generator for a piece and asked again when the
-- answer was the one already falling, so the promise is that the same piece never comes twice in a row —and the tries are a
-- handful so that a generator that keeps answering the same cannot hold the game still— (the old app's `takePiece`, docs/45).
local function takePiece()
  local name = Pieces[math.random(#Pieces)]

  for _ = 1, 8 do
    if name ~= lastPiece then break end
    name = Pieces[math.random(#Pieces)]
  end

  lastPiece = name
  return name
end

-- ---------------------------------------------------------------- the game

local function levelPoints() return PointsPerLevel * (level + 1) end

-- **The pace of the fall, out of the table and the floor of the panel.**
local function dropMs()
  local i = math.max(0, math.min(level, #RowFrames - 1))
  local wanted = math.floor(RowFrames[i + 1] * DropScale * 1000 / Fps)

  return math.max(RowFloorMs, wanted)
end

-- **I la cadència del CPU surt d'ella** (T163 of the old app): a row of the level is worth eight steps of the CPU.
local function cpuMs()
  return math.max(CpuFloorMs, math.min(CpuMaxMs, math.floor(dropMs() / CpuStepsPerRow)))
end

-- **The bar of the level says how much of this level is played**, which is why the level is points and not rows (docs/45):
-- twelve rows at any step of the ladder, and the bar and the number cannot disagree.
local function levelProgress()
  local step = demo and cpuLevel or level
  local base = demo and cpuBase or levelBase
  local asks = PointsPerLevel * (step + 1)
  local got = score - base

  if got <= 0 then return 0 end
  if got >= asks then return 1 end

  return got / asks
end

-- **A row of the piece that is on the board, painted or not**: the two calls that move it say what they left and what they
-- take, and `paintChanged` does the rest.
local function markPiece(tone, mode)
  if piece == nil then return end

  local cells = cellsOf(piece.name, piece.rot, piece.x, piece.y)

  for i = 1, #cells do mark(cells[i][1], cells[i][2], tone, mode) end
end

-- **The rows that are full**, from the bottom up: what the animation paints and what `clearLines` takes away.
local function fullRows()
  local out = {}

  for r = 1, Rows do
    local full = true

    for c = 1, Cols do
      if not cellAt(c, r) then full = false break end
    end

    if full then out[#out + 1] = r end
  end

  return out
end

-- **One row down, and the only place the game ends**: a piece that does not fit where it is born is the floor of the well
-- reached, and that is the only end this game has.
local function step()
  if piece == nil then return false end

  if fits(piece.name, piece.rot, piece.x, piece.y + 1) then
    markPiece(Paper, kStepMode)
    piece.y = piece.y + 1
    markPiece(Ink, kStepMode)
    noteTheTrail()
    return true
  end

  return false
end

-- **A move is one column and a turn is one state, and neither of them happens when it does not fit**: the original has no
-- wall kick, so a piece against the wall that cannot turn STAYS AS IT IS and the finger gets nothing (docs/45).
local function move(dx)
  if over or paused or piece == nil then return end

  if fits(piece.name, piece.rot, piece.x + dx, piece.y) then
    markPiece(Paper, kStepMode)
    piece.x = piece.x + dx
    markPiece(Ink, kStepMode)
    noteTheTrail()
  end
end

local function rotate(dir)
  if over or paused or piece == nil then return end

  local states = #Shapes[piece.name]
  local rot = (piece.rot - 1 + dir) % states + 1

  if rot == piece.rot then return end

  if fits(piece.name, rot, piece.x, piece.y) then
    markPiece(Paper, kStepMode)
    piece.rot = rot
    markPiece(Ink, kStepMode)
    noteTheTrail()
  end
end

local function save()
  if demo then return false end

  local parts = { string.format('%d %d %d %d %s', score, lines, level, levelBase, nextName) }

  for r = 1, Rows do
    local row = {}
    for c = 1, Cols do row[c] = cellAt(c, r) and '1' or '0' end
    parts[#parts + 1] = table.concat(row)
  end

  return fs.write(fs.data(FileName), table.concat(parts, '\n') .. '\n')
end

local function load()
  local text = fs.read(fs.data(FileName))
  if text == nil then return false end

  local it = text:gmatch('[^\n]+')

  local head = it()
  if head == nil then return false end

  local s, l, lv, base, next_one = head:match('^(%-?%d+) (%d+) (%d+) (%-?%d+) (%a+)$')
  if s == nil then return false end

  local read = {}
  local n = 0

  for line in it do
    n = n + 1
    if n > Rows then break end
    if #line ~= Cols or line:find('[^01]') then return false end
    read[n] = line
  end

  if n ~= Rows then return false end

  score, lines, level, levelBase = tonumber(s), tonumber(l), tonumber(lv), tonumber(base)
  nextName, lastPiece = next_one, nil
  well = {}

  for r = 1, Rows do
    for c = 1, Cols do
      if read[r]:sub(c, c) == '1' then setCell(c, r, true) end
    end
  end

  over = false

  return true
end

local function spawn()
  local name = nextName
  nextName = takePiece()

  -- **It enters at the top, with its highest block on the first row**: that is how the original put them in (docs/45).
  piece = { name = name, rot = 1, x = 4, y = 1 - topRowOf(name) }

  if not fits(piece.name, piece.rot, piece.x, piece.y) then
    over = true
    piece = nil
    return false
  end

  -- **I el naixement NO deixa cap marca** (0.190.256, l'amo: *«Les peces perden la forma quan giren!!!»*): a mark is a promise that
  -- somebody is going to paint it, and `lock` spawned and went away without painting, o sigui que les quatre marques de tinta es
  -- quedaven a la cua i **es pintaven al moviment següent, després de les de paper**: la peça tornava a tenir la forma de la casella
  -- d'on havia sortit i el que es veia era un blob de dues posicions. El naixement el pinta qui el fa (`paintPiece`), i les marques
  -- són només per al que es mou (docs/30, docs/45).
  cpuRot, cpuX = nil, nil

  return true
end

local function newGame()
  well = {}
  score, lines, level, levelBase = 0, 0, 0, 0
  over, paused = false, false
  piece = nil
  lastPiece = nil
  nextName = takePiece()
  cpuRot, cpuX = nil, nil
  spawn()
end

-- ---------------------------------------------------------------- the drawing

local function cellX(col) return wellX + (col - 1) * cell end
local function cellY(row) return wellY + (row - 1) * cell end

-- **One drawing for the whole game**: a cell is a square of ink with a pixel of paper on two of its sides, so a piece in the
-- air, a cell of the pile and a cell of the next box are the same square and a piece that lands does not change how it looks
-- (the owner, 16/10, docs/45). **I el paper del costat és el que fa que dues cel·les costat a costat es llegeixin com dues i
-- no com una barra.**
local function paintSquare(x, y, tone, mode)
  ui.rect(x, y, cell - Gap, cell - Gap, tone, mode)
end

-- **The four tones of a cell are its own tone and the paper that is already there**: erasing a cell is painting it paper,
-- and that is the only eraser this game has.
local function paintCellAt(col, row, tone, mode)
  if col < 1 or col > Cols or row < 1 or row > Rows then return end
  paintSquare(cellX(col), cellY(row), tone, mode)
end

-- **The well is the ink around and the paper inside**: the edge is painted first and the paper over it, so the edge is a band
-- of a known width around a well that is exactly `wellW` by `wellH` (the old app's own rule, docs/45).
local function paintWell(mode)
  ui.rect(wellX - Edge, wellY - Edge, wellW + 2 * Edge, wellH + 2 * Edge, Ink, mode)
  ui.rect(wellX, wellY, wellW, wellH, Paper, mode)
end

-- **The pile and the piece, all of them**: what the page does once and what a game does when it begins, and it is the same
-- call the piece uses, o sigui que no hi ha dos dibuixos que es puguin separar.
-- **El que cau, pintat i no marcat**: a piece is four boxes and they are painted where it is now, o sigui que el naixement i la
-- pàgina fan servir aquesta porta i les marques es queden per al que es mou (docs/30, docs/45).
local function paintPiece(mode)
  if piece == nil then return end

  local cells = cellsOf(piece.name, piece.rot, piece.x, piece.y)
  for i = 1, #cells do paintCellAt(cells[i][1], cells[i][2], Ink, mode) end
end

local function paintPile(mode)
  for r = 1, Rows do
    for c = 1, Cols do
      if cellAt(c, r) then paintCellAt(c, r, Ink, mode) end
    end
  end

  paintPiece(mode)
end

-- **El rectangle que la peça ha recorregut des de l'última neteja** (0.190.256): it grows with every cell the piece has stood on,
-- and it is what `cleanTheTrail` asks the glass to clean. A piece that is born opens it where it is born (docs/45).
function noteTheTrail()
  if piece == nil then return end

  local cells = cellsOf(piece.name, piece.rot, piece.x, piece.y)

  for i = 1, #cells do
    local c, r = cells[i][1], cells[i][2]

    if (c >= 1) and (c <= Cols) and (r >= 1) and (r <= Rows) then
      if not trailOpen then
        trailLeft, trailRight, trailTop, trailBottom = c, c, r, r
        trailOpen = true
      else
        if c < trailLeft then trailLeft = c end
        if c > trailRight then trailRight = c end
        if r < trailTop then trailTop = r end
        if r > trailBottom then trailBottom = r end
      end
    end
  end

  trailMoves = trailMoves + 1
end

-- **La neteja del rastre, quan toca**: the rectangle is painted again —paper where there is nothing, ink where the pile is and the
-- piece on top— and it goes up in the pass that **cleans**, one present. The number of movements is the original's and the flash is
-- what it pays for: dotze moviments i mig segon de passada, que és el que l'amo va triar allà perquè es pogués jugar (docs/45).
local function cleanTheTrail()
  if not trailOpen then return end

  for r = trailTop, trailBottom do
    for c = trailLeft, trailRight do
      paintCellAt(c, r, cellAt(c, r) and Ink or Paper, kCleanMode)
    end
  end

  paintPiece(kCleanMode)
  ui.present(kCleanMode)
  waitForTheInk()

  trailOpen, trailMoves = false, 0
end

-- **What changed, and nothing else**: at most the eight cells of a move or a turn, in one present of two tones.
local function paintChanged()
  if changedN == 0 then return end

  -- **EL DARRER QUE HA DIT QUÈ HI HA, I NO EL PRIMER** (0.190.255, l'amo amb el joc a la mà: *«al girar la peça canvia de forma!»*).
  -- A turn marks the cells the piece leaves in paper and the cells it takes in ink, and **a rotation keeps some of them where they
  -- are** —the `T` and the `L` keep two, the `I` keeps one—: with «the first mark of a cell wins», la marca de paper guanyava i el
  -- que es veia era la peça amb forats, o sigui una altra peça. El recorregut va del darrer al primer, i el darrer que parla d'una
  -- cel·la és el que hi ha de debò (docs/30, docs/45).
  local done = {}

  for i = changedN, 1, -1 do
    local box = changed[i]
    local key = box.col * 100 + box.row

    if not done[key] then
      done[key] = true
      paintCellAt(box.col, box.row, box.tone, box.mode)
    end
  end

  changedN = 0
  ui.present(kStepMode)
  waitForTheInk()

  if trailMoves >= TrailRows then cleanTheTrail() end
end

-- **The box of the next piece**: the square of it in a box of its own, which is a piece of the game and not a decoration.
local function paintNext(mode)
  if nextBoxW <= 0 then return end

  ui.rect(nextBoxX, nextBoxY, nextBoxW, nextBoxH, Paper, mode)

  local size = math.min(4 * cell, nextBoxW - 8, nextBoxH - 8)
  if size < 4 then size = 4 end

  local step = math.floor(size / 4)
  if step < 2 then step = 2 end

  local cells = cellsOf(nextName, 1, 1, 1)

  -- **The piece is centred in its box by its own cells** and not by the box of four by four: an `O` that was placed by the
  -- box would sit a cell to the right and a cell down of the middle (the old app's own rule).
  local minC, maxC, minR, maxR = 9, -9, 9, -9
  for i = 1, #cells do
    local c, r = cells[i][1], cells[i][2]
    if c < minC then minC = c end
    if c > maxC then maxC = c end
    if r < minR then minR = r end
    if r > maxR then maxR = r end
  end

  local pieceW = (maxC - minC + 1) * step
  local pieceH = (maxR - minR + 1) * step
  local ox = nextBoxX + math.floor((nextBoxW - pieceW) / 2)
  local oy = nextBoxY + math.floor((nextBoxH - pieceH) / 2)
  local gap = (step > 4) and 1 or 0

  for i = 1, #cells do
    local c, r = cells[i][1] - minC, cells[i][2] - minR
    ui.rect(ox + c * step, oy + r * step, step - gap, step - gap, Ink, mode)
  end
end

-- **The band of the middle, when the game is stopped**: with no card of the system, the page says it itself (the old app's
-- band, painted by this file, docs/45).
local function paintBand(mode)
  if (not over) and (not paused) then return end

  local words = over and 'GAME OVER' or 'PAUSED'
  local tw = ui.text_width(words, kBody)
  local lh = ui.line_height(kBody)
  local padX, padY = 16, 12
  local boxW = tw + 2 * padX
  local boxH = lh + 2 * padY
  local x = wellX + math.floor((wellW - boxW) / 2)
  local y = wellY + math.floor((wellH - boxH) / 2)

  ui.rect(x, y, boxW, boxH, Paper, mode)
  ui.frame(x, y, boxW, boxH, Ink, mode)
  ui.text(x + padX, y + padY, words, kBody, Ink, mode)
end

-- **The strip: the numbers and the bar of the level**, which is what a person reads while playing.
local function scoreText()
  return string.format('Score %d   Lines %d   %s %d', score, lines, demo and 'CPU' or 'L', demo and cpuLevel or level)
end

local function paceText() return string.format('%d ms a row', demo and cpuMs() or dropMs()) end

local function paintStatus(mode)
  if the_score ~= nil then the_score.text = scoreText() end
  if the_level ~= nil then the_level.text = paceText() end

  -- **The bar is a frame and its ink**, and it is as wide as the column of the numbers and not the page: the two buttons are
  -- on the right and a bar through them would be a bar over what can be touched (docs/45).
  ui.rect(Margin, barY, barW, BarH, Paper, mode)
  ui.frame(Margin, barY, barW, BarH, Ink, mode)

  local inside = barW - 2 * BarBorder
  local got = math.floor(inside * levelProgress())
  if got > 0 then ui.rect(Margin + BarBorder, barY + BarBorder, got, BarH - 2 * BarBorder, Ink, mode) end
end

-- **One thing at a time**: a game that is over is not paused, and a page nobody has touched says what a finger does.
local function footerText()
  if demo then return 'The CPU is playing. Tap the well to take over.' end
  if over then return 'The well is full. Tap New.' end
  if paused then return 'Paused. Tap Resume.' end
  if score == 0 then return 'Tap the piece to turn it, or its side to move it.' end

  return 'Tap the piece to turn it; left, right or below to move it.'
end

local function paintFooter(mode)
  if the_foot ~= nil then the_foot.text = footerText() end
  if the_pause ~= nil then the_pause.title = paused and 'Resume' or 'Pause' end
end

-- **The ink of a box is waited before the next is asked for** (`ui.idle`, docs/30): a box that arrives while the glass still
-- moves the one before is refused and stays owed.
function waitForTheInk()
  for _ = 1, 24 do
    if ui.idle() then return end
    sys.delay(40):await()
  end
end

-- ---------------------------------------------------------------- a row that is full

-- **A full row is painted, blinked and taken away** (the owner, 16/10: *«en cas de col·lapsar una linia: fas blink i fas
-- desapareixer la linia»*): two passes of two tones —the row goes white and comes back— and then the pile comes down in the
-- pass of the whole well, without an effect of its own (the old app's animation, with the ink and the paper of this port).
local function blinkRows(rows)
  if #rows == 0 then return end

  for frame = 1, 2 do
    local tone = (frame == 1) and Paper or Ink

    for i = 1, #rows do
      for c = 1, Cols do paintCellAt(c, rows[i], tone, kStepMode) end
    end

    ui.present(kStepMode)
    waitForTheInk()
  end
end

-- **The rows go away and the pile comes down.** A `while` and not a `for`: when a row disappears the one above comes down to
-- take its place and that row has not been looked at yet, which a `for` would step over and leave a full row forever (the old
-- app's own note, docs/45).
local function clearLines()
  local cleared = 0
  local r = Rows

  while r >= 1 do
    local full = true

    for c = 1, Cols do
      if not cellAt(c, r) then full = false break end
    end

    if full then
      cleared = cleared + 1

      for rr = r, 2, -1 do
        for c = 1, Cols do setCell(c, rr, cellAt(c, rr - 1)) end
      end

      for c = 1, Cols do setCell(c, 1, nil) end
    else
      r = r - 1
    end
  end

  return cleared
end

-- **The tape of the piece, which is the only thing that changes what is on the glass**: a landing is the one moment the game
-- does not paint the cells of the piece, because a cell of the game and a cell of the pile are the same square (see the head
-- of this file). What it does paint is the rows that go away.
local function lock()
  if piece == nil then return end

  local cells = cellsOf(piece.name, piece.rot, piece.x, piece.y)

  for i = 1, #cells do
    local c, r = cells[i][1], cells[i][2]
    if r >= 1 then setCell(c, r, true) end
  end

  piece = nil
  cpuRot, cpuX = nil, nil

  local rows = fullRows()

  if #rows == 0 then
    -- **El naixement, la seva tinta i la línia**: the state is the piece that has come and not the one that went, or a case
    -- cannot tell a landing from a game that has ended (docs/45). **I el naixement es pinta aquí perquè aquí es fa**: una marca
    -- que es queda a la cua es pinta al moviment següent i la peça es veu a dues posicions alhora (0.190.256).
    trailOpen, trailMoves = false, 0        -- the piece that went is the pile now: its trail is closed and does not travel
    spawn()
    paintPiece(kStepMode)
    ui.present(kStepMode)
    waitForTheInk()
    noteTheTrail()
    console.print(stateLine())
    return
  end

  blinkRows(rows)

  local cleared = clearLines()
  score = score + LineScore[math.min(cleared, 4)] * (level + 1)
  lines = lines + cleared

  -- **A clear can carry a level and more than one**: four rows at level fifteen pay 19 200 points, and those are the two
  -- levels that ask for 8 000 and 8 500; each step pays for itself before the next one is asked for (docs/45).
  --
  -- **And the demo climbs a ladder of its own**: a demonstration that climbed the person's ladder would be a demonstration of
  -- the person's game, and the person was not playing (the old app's rule, docs/45).
  while (demo and cpuLevel < LevelMax) and (score - cpuBase >= PointsPerLevel * (cpuLevel + 1)) do
    cpuBase = cpuBase + PointsPerLevel * (cpuLevel + 1)
    cpuLevel = cpuLevel + 1
  end
  while (not demo) and (level < LevelMax) and (score - levelBase >= levelPoints()) do
    levelBase = levelBase + levelPoints()
    level = level + 1
  end

  -- **And the whole well is painted once**, because a collapse moves the pile: the paper, the pile and the piece that comes.
  trailOpen, trailMoves = false, 0
  spawn()
  noteTheTrail()
  changedN = 0                    -- the page below paints it, o sigui que les marques del naixement no han de fer un segon viatge
  paintWell(kStepMode)
  paintPile(kStepMode)
  ui.present(kStepMode)
  waitForTheInk()

  -- **The numbers are said when they move**, and a level that goes up is a strip that has to say it now and not in a turn.
  paintStatus(kTextMode)
  ui.present(kTextMode)
  waitForTheInk()

  -- **And the state is said when a piece lands**, which is one line every few seconds and not one per turn: a test reads a
  -- clear from here instead of guessing it from a picture (docs/45). It is said with the piece that has come, or a case
  -- cannot tell a landing from a game that has ended.
  if over then
    console.print(stateLine())
    save()
    paintBand(kTextMode)
    paintFooter(kTextMode)
    ui.present(kTextMode)
    waitForTheInk()
    return
  end

  console.print(stateLine())
end

-- ---------------------------------------------------------------- the CPU of the demo

-- **It is a player of this same game and not a script**: it reads the well the way `clearLines` reads it, it lands its piece
-- with the same `fits` and `cellsOf` the person's piece uses, and it takes the four states of `Shapes`, so the day the pieces
-- change the CPU learns them with nobody touching it (the old app's rule, docs/45).
local function boardScore()
  local heights = {}
  local holes = 0
  local full = 0

  for c = 1, Cols do
    local h, seen = 0, false

    for r = 1, Rows do
      if cellAt(c, r) then
        if not seen then h, seen = Rows - r + 1, true end
      elseif seen then
        holes = holes + 1
      end
    end

    heights[c] = h
  end

  for r = 1, Rows do
    local n = 0
    for c = 1, Cols do
      if cellAt(c, r) then n = n + 1 end
    end
    if n == Cols then full = full + 1 end
  end

  local aggregate, bumpy = 0, 0

  for c = 1, Cols do
    aggregate = aggregate + heights[c]
    if c < Cols then bumpy = bumpy + math.abs(heights[c] - heights[c + 1]) end
  end

  return EvalLine * full + EvalHeight * aggregate + EvalHole * holes + EvalBumpy * bumpy
end

-- **Where it wants this piece**: a state and a column, chosen by letting it fall in every place and scoring the board that
-- comes out. The piece is dropped from above the board so that a state whose box is taller is not put lower than it belongs,
-- and the candidate that cannot be placed at all is skipped by `fits` itself (the old app's `cpuChoose`, docs/45).
local function cpuChoose()
  if piece == nil then return nil, nil end

  local name = piece.name
  local best, bestRot, bestX = nil, nil, nil
  local saved = {}

  for k, v in pairs(well) do saved[k] = v end

  for rot = 1, #Shapes[name] do
    for x = -3, Cols do
      local y = -3

      if fits(name, rot, x, y) then
        while fits(name, rot, x, y + 1) do y = y + 1 end

        local cells = cellsOf(name, rot, x, y)
        local put = {}

        for i = 1, #cells do
          local c, r = cells[i][1], cells[i][2]
          if r >= 1 then
            put[idx(c, r)] = true
            well[idx(c, r)] = true
          end
        end

        local got = boardScore()

        for i = 1, #cells do
          local c, r = cells[i][1], cells[i][2]
          if r >= 1 then well[idx(c, r)] = saved[idx(c, r)] end
        end

        if (best == nil) or (got > best) then
          best, bestRot, bestX = got, rot, x
        end
      end
    end
  end

  return bestRot, bestX
end

-- **One step towards its target, and down once it is there.** A step the game refuses is not fought: the CPU takes where the
-- piece is and drops it there, because a player that insists on a move the rules do not allow is a player that stops playing
-- (the old app's `cpuStep`, docs/45).
local function cpuStep()
  if piece == nil then return end

  if cpuRot == nil then cpuRot, cpuX = cpuChoose() end
  if cpuRot == nil then return end

  if piece.rot ~= cpuRot then
    local before = piece.rot
    rotate(1)
    if piece.rot == before then cpuRot, cpuX = piece.rot, piece.x end
    return
  end

  if piece.x ~= cpuX then
    local before = piece.x
    move((piece.x < cpuX) and 1 or -1)
    if piece.x == before then cpuRot, cpuX = piece.rot, piece.x end
    return
  end

  if not step() then lock() end
end

-- ---------------------------------------------------------------- the geometry

local function measure(w, h)
  -- **La franja es mesura i no s'endevina** (docs/30, docs/45): two lines of text, the bar of the level and the air.
  local lineS = ui.line_height(kSmall)
  stripH = 6 + 2 * lineS + 2 + 8 + BarH + 6
  if stripH < Strip then stripH = Strip end

  -- **A column and a box**: the well takes what is left after the box of the next piece, and the cell is the height that fits
  -- eighteen rows, whichever is smaller (a well wider than the glass is a well whose last column nobody sees).
  local byH = math.floor((h - stripH - Footer - 2 * Edge) / Rows)
  local byW = math.floor((w - 2 * Margin - 2 * Edge - NextReserve) / Cols)
  cell = math.min(byH, byW)
  if cell < 6 then cell = 6 end

  wellW, wellH = cell * Cols, cell * Rows

  local left = Margin + Edge
  local space = w - Margin - NextReserve - left
  wellX = left + math.max(0, math.floor((space - wellW) / 2))
  wellY = stripH + Edge + math.max(0, math.floor((h - stripH - Footer - 2 * Edge - wellH) / 2))

  nextBoxX = wellX + wellW + Edge + 10
  nextBoxW = w - Margin - nextBoxX
  nextBoxH = nextBoxW
  nextBoxY = stripH + Edge + 9
  if nextBoxW < 40 then nextBoxW, nextBoxH = 0, 0 end

  -- **Els botons es mesuren de la seva paraula** (docs/30, docs/45): a pixel of difference is a word with dots at the end.
  newW = ui.text_width('New', kSmall) + 2 * BtnPad + 2
  if newW < BtnMinW then newW = BtnMinW end
  newX = w - Margin - newW
  newY = 6

  pauseW = ui.text_width('Resume', kSmall) + 2 * BtnPad + 2
  if pauseW < BtnMinW then pauseW = BtnMinW end
  pauseX = newX - 8 - pauseW
  if pauseX < Margin then pauseX = Margin end
  pauseY = 6

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

  -- **La línia dels números té el que deixen els tres botons**, i la de baix tota l'amplada.
  barW = demoX - Margin - 8
  if barW < 40 then barW = 40 end
  barY = stripH - 6 - BarH

  if the_score == nil then
    the_score = ui.label(barW, lineS)
    if the_score ~= nil then
      the_score.scale = kSmall
      the_score.align = 'left'
      the_score.valign = 'middle'
    end
  else
    the_score:resize(barW, lineS)
  end
  if the_score ~= nil then the_score:place(Margin, 6) end

  if the_level == nil then
    the_level = ui.label(barW, lineS)
    if the_level ~= nil then
      the_level.scale = kSmall
      the_level.align = 'left'
      the_level.valign = 'middle'
    end
  else
    the_level:resize(barW, lineS)
  end
  if the_level ~= nil then the_level:place(Margin, 6 + lineS + 2) end

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
  if the_foot ~= nil then the_foot:place(Margin, wellY + wellH + Edge + 8) end
end

-- ---------------------------------------------------------------- the finger

-- **The page is built from scratch in one place**: the paper, the well, the pile, the box of the next piece, the strip and
-- the line, and the system asks every control it covered to draw again when the paper is laid (docs/30, docs/50).
function paintPage()
  ui.clear(Paper)
  ui.rect(0, 0, W, H, Paper, kPageMode)
  paintWell(kPageMode)
  paintPile(kPageMode)
  paintNext(kPageMode)
  paintBand(kPageMode)
  paintStatus(kPageMode)
  paintFooter(kPageMode)
  ui.present(kPageMode)
  waitForTheInk()

  -- **I les marques que hi hagués es tornen a zero**: la pàgina ho ha pintat tot, i una marca que sobreviu a la pàgina és una
  -- caixa que es pinta un altre cop sense que res hagi canviat (docs/30, docs/45).
  changedN = 0
  lastScore, lastLevel, lastFoot = score, (demo and cpuLevel or level), footerText()
end

-- **A tap on the piece turns it, and a tap beside it moves it**: where the finger lands says what it means from where the
-- piece is, and the axis with more travel decides. **A tap above it turns it too**, because up is not a way a piece goes —
-- the four ways of the original are the three of a finger and a turn— (the owner, 22/11).
local function tapWell(x, y)
  -- **Un toc al pou durant una demostració la pren**: the old app's rule was «any key of the person hands it over», and here
  -- the key is the finger (docs/30, docs/45).
  if demo then
    -- **I el comandament torna amb l'escala del joc que hi ha al vidre**: a person who takes over a demonstration keeps the board
    -- and the points, o sigui que el nivell que es juga és el que el CPU havia pujat i no el zero de la partida d'abans (docs/45).
    demo = false
    level, levelBase = cpuLevel, cpuBase
    cpuRot, cpuX = nil, nil
    save_state()
    console.print(stateLine())
    paintFooter(kTextMode)
    ui.present(kTextMode)
    waitForTheInk()
    return true
  end

  if over or paused or piece == nil then return false end

  local cells = cellsOf(piece.name, piece.rot, piece.x, piece.y)
  local minC, maxC, minR, maxR = 99, -99, 99, -99

  for i = 1, #cells do
    local c, r = cells[i][1], cells[i][2]
    if c < minC then minC = c end
    if c > maxC then maxC = c end
    if r < minR then minR = r end
    if r > maxR then maxR = r end
  end

  local cx = cellX(minC) + math.floor((maxC - minC + 1) * cell / 2)
  local cy = cellY(minR) + math.floor((maxR - minR + 1) * cell / 2)
  local dx, dy = x - cx, y - cy

  local halfW = math.floor((maxC - minC + 1) * cell / 2) + kTapPx
  local halfH = math.floor((maxR - minR + 1) * cell / 2) + kTapPx

  -- **On the piece is a turn**: the box it occupies, holes included, which is what a finger means when it presses a shape.
  if (math.abs(dx) <= halfW) and (math.abs(dy) <= halfH) then
    rotate(1)
    console.print(stateLine())
    return true
  end

  if math.abs(dx) >= math.abs(dy) then
    move((dx > 0) and 1 or -1)
  elseif dy < 0 then
    rotate(1)                      -- up is not a way a piece goes
  else
    -- **Below is one row down, and the floor is the landing**: a soft drop that does not move is a piece that has arrived.
    if not step() then lock() end
  end

  console.print(stateLine())
  return true
end

-- **Els tres botons es miren aquí i no al sistema** (they are `focusable = false`, so the finger arrives at this file), which
-- is what leaves the centre of the dial to the system (docs/30).
local function tapAt(x, y)
  if (x >= newX) and (x < newX + newW) and (y >= newY) and (y < newY + BtnH) then
    paused, demo = false, false
    cpuRot, cpuX = nil, nil
    newGame()
    save()
    save_state()
    paintPage()
    console.print(stateLine())

    return true
  end

  if (x >= demoX) and (x < demoX + demoW) and (y >= pauseY) and (y < pauseY + BtnH) then
    newGame()
    demo = true
    cpuLevel, cpuBase = 0, 0
    paused = false
    cpuRot, cpuX = nil, nil
    paintPage()
    console.print(stateLine())

    return true
  end

  if (x >= pauseX) and (x < pauseX + pauseW) and (y >= pauseY) and (y < pauseY + BtnH) then
    if over then return false end

    paused = not paused
    save_state()
    paintPage()
    console.print(stateLine())

    return true
  end

  if (x >= wellX) and (x < wellX + wellW) and (y >= wellY) and (y < wellY + wellH) then
    if tapWell(x, y) then
      paintChanged()
      -- **La tira, quan diu una altra cosa**: a clear moves the numbers and the bar, and it is one box and one pass.
      local shown = demo and cpuLevel or level
      if (score ~= lastScore) or (shown ~= lastLevel) then
        paintStatus(kTextMode)
        ui.present(kTextMode)
        waitForTheInk()
        lastScore, lastLevel = score, shown
      end
      return true
    end
  end

  return false
end

-- ---------------------------------------------------------------- the app

local caps = sys.caps()
if (caps == nil) or not caps.display then
  console.print('tetris: this board has no glass')
  return
end

-- **Els tres tocs del vidre són d'aquest joc i no del sistema** (0.190.254, l'amo: *«Desactiva els 3 tocs sobre el Tetris, l'Snake
-- i qualsevol joc que no el tingui desactivat»*). Un joc llegeix cada toc, o sigui que el tercer no pot ser una ordre del sistema
-- que tanqui la partida al mig; el gest queda a la cua i aquest bucle el deixa caure, perquè només mira `tap` i `drag`. **I tornar
-- enrere no es perd**: les tres premsades de la tecla del mig són un altre camí i no les toca ningú, i quan l'app s'acaba el
-- sistema recupera els tres tocs sol (docs/30, docs/46).
if (touch.triple_tap_close ~= nil) then touch.triple_tap_close(false) end

W, H = ui.caps().width, ui.caps().height

-- **La forma de la vista, abans de col·locar res** (docs/30): whether the page was stopped is read here, and a block of a
-- shape this build does not know is thrown away.
local shape = load_state()
if (shape ~= nil) and (shape.paused ~= nil) then paused = (shape.paused == '1') end

-- **I el joc torna on era**, i una placa sense partida desada és una partida nova —no un error— (docs/45).
if not load() then newGame() end

measure(W, H)

-- **`run tetris keys` diu la geometria** so a test asks instead of guessing a corner, and `seed=N` fixes the machine's
-- numbers (docs/45).
local asked = (app.args ~= nil) and tostring(app.args.path or '') or ''
-- **La paraula i els números són dues coses**: `app.args.path` és el que es va escriure després del nom de l'app, i una prova
-- hi pot posar `demo seed=7` mentre que el prompt només hi deixa passar un mot; o sigui que el verb és el primer i els números
-- es busquen a tota la cadena (docs/30, docs/45).
local verb = asked:match('^[^ ]*') or ''

local seed = asked:match('seed=(%d+)')
if (seed ~= nil) and (math.randomseed ~= nil) then math.randomseed(tonumber(seed)) end

if verb == 'keys' then
  console.print(string.format('[tetris] cell %d, well %d,%d %dx%d, next %d,%d %dx%d, new %d,%d %dx%d, pause %d,%d %dx%d, demo %d,%d %dx%d',
    cell, wellX, wellY, wellW, wellH, nextBoxX, nextBoxY, nextBoxW, nextBoxH, newX, newY, newW, BtnH, pauseX, pauseY, pauseW,
    BtnH, demoX, pauseY, demoW, BtnH))
end

-- **`stack=N` is a well in a state a person can read**: the bottom `N` rows are full except the column of the `I`, which is
-- put there standing up with its last row on the top of the stack —so one tap below it lands it and takes `N` rows away—.
-- It is how a test sees the blink, the score of a clear and the bar of the level without playing four hundred pieces (docs/45).
local stack = asked:match('stack=(%d+)')
if stack ~= nil then
  local n = math.max(1, math.min(4, tonumber(stack)))

  newGame()
  well = {}

  for r = Rows - n + 1, Rows do
    for c = 1, Cols do
      if c ~= 5 then setCell(c, r, true) end
    end
  end

  -- **The `I` standing up, in its own column**: its four cells are the column two of its box, o sigui que `x = 3` puts them
  -- on the fifth column of the well and the state `2` is the one that is a column. **It is one row above resting** so that a
  -- finger still has the bottom of the well to point at —a piece on the floor has no «below»—, i dues baixades el deixen a
  -- sobre de la pila: the first one moves it, the second cannot and locks it, which is the rule of this port.
  piece = { name = 'I', rot = 2, x = 3, y = Rows - 4 }
  nextName = 'O'
  lastPiece = 'I'
  over, paused = false, false
  cpuRot, cpuX = nil, nil
  score, lines, level, levelBase = 0, 0, 0, 0
end

-- **`full` is a page to read**: a well with a pile in it, a piece in the air and a score that is not zero, which is the only
-- way a test sees the strip, the bar and the pile together without playing (docs/45).
if verb == 'full' then
  newGame()
  well = {}

  for c = 1, Cols do
    setCell(c, Rows, true)
    setCell(c, Rows - 1, true)
  end
  for c = 1, 4 do setCell(c, Rows - 2, true) end
  setCell(1, Rows - 3, true)

  piece = { name = 'T', rot = 1, x = 4, y = Rows - 8 }
  nextName = 'S'
  score, lines, level, levelBase = 300, 3, 1, 0
  over, paused = false, false
end

-- **`demo` és el CPU jugant**, que és l'única manera que una prova el vegi.
local bench = (verb == 'demo')
if bench then
  newGame()
  demo = true
  cpuLevel, cpuBase = 0, 0
  paused = false
end

-- **L'estat dit en una línia**: el que un cas llegeix en lloc d'endevinar-ho mirant un dibuix (docs/45).
function stateLine()
  return string.format('[tetris] x=%d y=%d rot=%d piece=%s lines=%d score=%d level=%d over=%d paused=%d demo=%d',
    (piece ~= nil) and piece.x or -1, (piece ~= nil) and piece.y or -1, (piece ~= nil) and piece.rot or -1,
    (piece ~= nil) and piece.name or '-', lines, score, demo and cpuLevel or level, over and 1 or 0, paused and 1 or 0,
    demo and 1 or 0)
end

paintPage()
console.print(stateLine())

function on_pause() save(); save_state() end
function on_save() save(); save_state() end
function on_close() save(); save_state() end

-- **The clock of the game is a date and not the wait of the turn**: the row is due at `nextDrop` and the loop looks at the
-- finger every sixteen milliseconds, so a tap lands while the piece is between two rows and does not wait for the fall.
-- **And what the glass takes to paint is not taken off the pace**: the present is 120 ms and the floor of a row is 200, so
-- the next row is counted from the last one and not from the end of the paint (docs/30, docs/95).
local nextDrop = sys.time_ms() + dropMs()
local nextCpu = sys.time_ms() + cpuMs()

while true do
  local view = ui.caps()
  if (view.width ~= W) or (view.height ~= H) then
    W, H = view.width, view.height
    measure(W, H)
    paintPage()
  end

  -- **Els gestos es prenen i la cua es buida**: a gesture nobody takes is a queue that grows (docs/30).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'tap' then
      tapAt(gesture.x or 0, gesture.y or 0)
    elseif gesture.kind == 'drag' then
      if (math.abs(gesture.dx or 0) < TapSlop) and (math.abs(gesture.dy or 0) < TapSlop) then
        tapAt(gesture.x or 0, gesture.y or 0)
      end
    end

    gesture = touch.gesture()
  end

  -- **The strip and the line of the bottom, when they say something else**: one box and one pass each.
  local shown = demo and cpuLevel or level
  if (score ~= lastScore) or (shown ~= lastLevel) then
    paintStatus(kTextMode)
    ui.present(kTextMode)
    waitForTheInk()
    lastScore, lastLevel = score, shown
  end

  local foot = footerText()
  if foot ~= lastFoot then
    paintFooter(kTextMode)
    ui.present(kTextMode)
    waitForTheInk()
    lastFoot = foot
  end

  if (not over) and (not paused) then
    local now = sys.time_ms()

    if demo then
      -- **El CPU juga al ritme del nivell**, and every step says the state: `demo` is a bench and not a person's game, and a
      -- case that read only the beginning would measure the clock and not the player (the old app's `-probe`, docs/45).
      if now >= nextCpu then
        cpuStep()
        paintChanged()
        if bench then console.print(stateLine()) end
        nextCpu = now + cpuMs()
      end
    elseif now >= nextDrop then
      -- **The row falls, and the piece that has arrived is locked**: one row per turn and not a jump, because a turn that
      -- arrives late —a busy system, a pass of the panel in the middle— must not become seven rows at once (docs/45).
      if step() then
        paintChanged()
      else
        lock()
      end

      nextDrop = nextDrop + dropMs()
      local after = sys.time_ms()
      if (nextDrop <= after) or ((nextDrop - after) > 4 * dropMs()) then nextDrop = after + dropMs() end
    end
  else
    nextDrop = sys.time_ms() + dropMs()
    nextCpu = sys.time_ms() + cpuMs()
  end

  sys.delay(16):await()
end
