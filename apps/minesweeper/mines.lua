-- mines: the field of mines, on electronic ink.
--
-- A grid of nine by nine with ten mines and the rule everybody knows: a tap opens a cell, the number it shows is how many
-- mines touch it, an empty cell opens its neighbours, and a flag says «here there is one». **The first tap of a game is
-- always safe**: the mines are laid after it and neither it nor its eight neighbours can have one —an e-ink board is not a
-- place to lose a game on a tap whose numbers nobody could have read yet.
--
-- **THE GREYS ARE THE WHOLE OF THE DRAWING.** A cell that is closed is light grey with a darker frame —the relief a person
-- reads as «not yet»—, a cell that is open is the paper with a thin line so the grid is still a grid, and the digit of a
-- cell is a **step of ink**: one is a soft grey and eight is black. There is no colour to tell a three from a five on this
-- glass, and the step is what makes it read at arm's length (docs/30, docs/92).
--
-- **A CELL IS ONE BOX AND THE BOARD DOES NOT TRAVEL** (0.189.7): an open cell, a flag and a mine are painted one by one,
-- and a field of thirty cells opened at once is thirty boxes in **one** present —the kernel merges them— and not a page of
-- five hundred pixels per move (docs/30, docs/50).
--
-- **THE GAME IS DATA AND WHAT A TAP MEANS IS FORM.** The field, the flags and whether the game is over are a game, and a
-- game lives in the app's own file (`fs.data('game.txt')`) and comes back exactly where it was. The switch of the strip —
-- whether a tap opens a cell or puts a flag— is the shape of the page and it goes to the `state` block, so a person who
-- left it on finds it on (docs/30, docs/45 §5 bis).
--
-- **NO CONTROL TAKES THE CENTRE KEY.** The button and the switch are `focusable = false` and this file reads the finger
-- itself, which keeps the three presses of the centre as the system's way out of an app (docs/30).

local N = 9                     -- the field is 9 x 9
local Mines = 10                -- and it holds ten mines
local Gap = 4                   -- between cells, in pixels
local Strip = 76                -- the top strip: the counter, the flag switch and the new game
local Footer = 44               -- the line at the bottom: what the game is saying
local BtnH = 63                 -- the height of a button, which is the least a finger can hit
local BtnPad = 16               -- the room a button of this system leaves its word on each side
local BtnMinW = 84              -- never narrower than this, even when the word would fit in less
local Margin = 12               -- the paper on the sides
local FileName = 'game.txt'     -- the game, inside this app's own folder

-- **The capsule of a switch is the core's and it is 60 px wide plus four of air** (docs/30, docs/90), and the words of the
-- row sit after it: the number is the kernel's and not this file's, and a row measured from a guess is a word cut in half.
local SwitchBox = 64
local SwitchH = 48

-- **The three qualities of a paint** (docs/50, docs/92): the page is sixteen levels and one pass, a cell that changes is
-- sixteen levels too —a digit is not a two-level thing and the greys have to land on the grey they had—, and a message
-- that changes is a pass that **cleans**, because a sentence that goes away and another that arrives are not one ink.
local kPageMode = 'gl16'
local kCellMode = 'gld16'
local kClearMode = 'gl16'

-- **The step of ink for a digit**, by how many mines it counts: one is a soft grey and eight is black.
local NumberInk = { 10, 8, 6, 5, 4, 3, 2, 0 }

local HiddenFill = 11           -- a cell nobody has opened: light grey...
local HiddenEdge = 8            -- ...with a darker frame, which is what makes it look raised
local OpenFill = 15             -- a cell that is open is the paper
local OpenEdge = 13             -- and its frame is a line, so the grid is still a grid

-- **The sizes of the paper's face, in pixels of line** (docs/45): small 33 and body 43, asked for by the size and not by
-- the step, so the page keeps the same physical size as the other apps (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  return math.max(1, math.min(16, math.floor(line_px / step + 0.5)))
end

local kSmall = scale_for(33)
local kBody = scale_for(43)

-- **A drag is not a tap**: a finger that slides on a page of cells is a hand that meant to scroll, and opening a cell
-- under it would be a mine stepped on by accident (docs/30).
local kLongPx = 12

-- ---------------------------------------------------------------- what two functions share

-- **Tot el que dues funcions comparteixen es declara aquí** and not next to the first one that wants it: a `local` declared
-- after a function is a **global** inside it, and this file learned it the way every Lua file does —a switch that came back
-- off, a page that painted a nil— (docs/30, docs/45). It is one block and it is at the top on purpose.

local state = nil               -- the `state` block: asked once and kept for the life of the app
local flagMode = false          -- what a tap means: open a cell, or put a flag on it

local mine = {}                 -- 1..N*N: true when the cell holds a mine
local open = {}                 -- and true when it has been opened
local flag = {}                 -- and true when a flag is on it
local placed = false            -- the mines are laid after the first tap, which is what makes it safe
local over = false              -- the game is lost or won
local won = false               -- and it was won
local left = Mines              -- how many mines are still without a flag

local dirty = {}                -- the cells the last move changed, painted once
local lastFooter = ''           -- what the line of the bottom said on the glass
local lastLeft = 0              -- and the number of the strip, which is a label and not a cell
local help = ''                 -- what the game says when the file cannot be written

local cell, boardW, boardX, boardY          -- the geometry, measured for this view
local btnX, btnY, btnW                      -- the button of a new game
local flagX, flagY, flagW, flagH            -- the switch of the strip
local the_button, the_flag, the_left, the_foot, the_flag_words
local W, H = 0, 0

-- ---------------------------------------------------------------- the state of the page (form)

local kStateBytes = 1024
local kLayout = 'v1'            -- bumped when a control is renamed or the page changes shape

local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end

  return state
end

-- **What the page is now**: the switch of the strip, and nothing else. The field is the game and it lives in `fs.data`,
-- because a game is data and a switch is the shape of the page (docs/30, docs/45).
local function save_state()
  local block = the_state()
  if block == nil then return false end

  block:clear()
  block:write(0, kLayout .. '\nflag=' .. (flagMode and '1' or '0'))

  return true
end

-- **What the page was**: `nil` when there is nothing to read back or the block is of another shape, which is a page that
-- starts as a new one and not a ring on a control that no longer exists.
local function load_state()
  local block = the_state()
  if block == nil then return nil end

  local body = block:read(0, kStateBytes) or ''
  -- **El text s'acaba a la primera marca de zero** (0.189.52): the block is a kilobyte and what is written is shorter, so
  -- what comes behind is the zero the block was made with, and a line with zeros behind it is not a value.
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end

  local now = {}
  local first = true

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

-- ---------------------------------------------------------------- the game

-- **A field with nothing in it**: the two rules of the game start here and a new game is this and nothing else.
local function newGame()
  for i = 1, N * N do
    mine[i] = false
    open[i] = false
    flag[i] = false
  end

  placed, over, won, left = false, false, false, Mines
  dirty = {}
end

-- **The eight cells that touch one**, by row and column and not by an offset of the flat index: one more on the last
-- column is the first cell of the next row, which is a mine that is not touching anything (the bug every minesweeper has
-- had once).
local function eachNeighbour(i, fn)
  local row = math.floor((i - 1) / N) + 1
  local col = (i - 1) % N + 1

  for dr = -1, 1 do
    for dc = -1, 1 do
      if not ((dr == 0) and (dc == 0)) then
        local r, c = row + dr, col + dc
        if (r >= 1) and (r <= N) and (c >= 1) and (c <= N) then fn((r - 1) * N + c) end
      end
    end
  end
end

local function aroundOf(i)
  local count = 0
  eachNeighbour(i, function(j) if mine[j] then count = count + 1 end end)

  return count
end

-- **The mines are laid when a finger opens the first cell** and not before: it is the only way the first tap can be safe
-- on a field whose numbers nobody has seen (docs/30).
local function placeMines(safe)
  local banned = { [safe] = true }
  eachNeighbour(safe, function(j) banned[j] = true end)

  local pool = {}
  for i = 1, N * N do
    if not banned[i] then pool[#pool + 1] = i end
  end

  for _ = 1, math.min(Mines, #pool) do
    local pick = math.random(#pool)
    mine[pool[pick]] = true
    table.remove(pool, pick)
  end

  placed = true
end

local function mark(i) dirty[#dirty + 1] = i end

-- **Tot el que no és mina és obert** is the win, and a flag that is not on a mine does not count: a game that cannot be
-- finished because a flag is in the wrong cell is a game a person stops playing.
local function checkWin()
  for i = 1, N * N do
    if not mine[i] and not open[i] then return end
  end

  won, over = true, true
  for i = 1, N * N do
    if mine[i] and not flag[i] then
      flag[i] = true
      left = left - 1
      mark(i)
    end
  end
end

-- **Opening a cell opens what it has to open**: a cell with a number is itself, and an empty one is itself and its
-- neighbours, in a stack and not in recursion —a field of eighty cells would be eighty frames of a Lua stack on a board
-- whose interpreter has fifty kilobytes (docs/75).
local function openCell(i)
  if over or open[i] or flag[i] then return false end
  if not placed then placeMines(i) end

  if mine[i] then
    open[i] = true
    mark(i)
    over = true
    -- **I el camp sencer es veu quan es perd**: a board that says «boom» and hides where the mines were asks a person to
    -- lose the same way twice.
    for j = 1, N * N do
      if mine[j] and not open[j] then
        open[j] = true
        mark(j)
      end
    end

    return true
  end

  local stack = { i }
  while #stack > 0 do
    local at = table.remove(stack)
    if not open[at] and not flag[at] then
      open[at] = true
      mark(at)
      if aroundOf(at) == 0 then
        eachNeighbour(at, function(j)
          if not open[j] and not flag[j] then stack[#stack + 1] = j end
        end)
      end
    end
  end

  checkWin()

  return true
end

-- **A flag is put and taken with the same finger**, and the counter of the strip is what says how many are left: a person
-- who flags more than there are mines is telling the game something it cannot believe, and the number goes below zero and
-- says so.
local function toggleFlag(i)
  if over or open[i] then return false end

  flag[i] = not flag[i]
  left = left + (flag[i] and -1 or 1)
  mark(i)

  return true
end

-- **Tocar un número que ja és obert és obrir el que li falta** —the chord of every minesweeper— i només quan les banderes
-- del voltant són exactament el número: el que hi ha és el que la persona ja ha dit, i el que hi falta és el que vol obrir.
local function chord(i)
  if over or not open[i] then return false end

  local count = aroundOf(i)
  if count == 0 then return false end

  local flags = 0
  eachNeighbour(i, function(j) if flag[j] then flags = flags + 1 end end)
  if flags ~= count then return false end

  local opened = false
  eachNeighbour(i, function(j)
    if not open[j] and not flag[j] then
      if openCell(j) then opened = true end
    end
  end)

  return opened
end

-- ---------------------------------------------------------------- the file

-- **One line per thing**, the same shape as the save of every app (docs/45): the size of the field first, so a file of
-- another field —another build, another game— is a new game and not a page with the wrong numbers on it.
local function save()
  local lines = { tostring(N), tostring(Mines) }
  local cells, opens, flags = {}, {}, {}

  for i = 1, N * N do
    cells[i] = mine[i] and '1' or '0'
    opens[i] = open[i] and '1' or '0'
    flags[i] = flag[i] and '1' or '0'
  end

  lines[#lines + 1] = table.concat(cells)
  lines[#lines + 1] = table.concat(opens)
  lines[#lines + 1] = table.concat(flags)
  lines[#lines + 1] = placed and '1' or '0'
  lines[#lines + 1] = over and '1' or '0'
  lines[#lines + 1] = won and '1' or '0'

  local ground = fs.data()
  if ground ~= nil then pcall(fs.mkdir, ground) end

  local written, reason = fs.write(fs.data(FileName), table.concat(lines, '\n'))
  if written == nil then
    -- **El codi i no la taula** (0.190.24): `fs.write` answers `nil, {code, message}` and a table printed on the glass
    -- says nothing.
    local said = (type(reason) == 'table') and (reason.code or reason.message) or reason
    help = 'Could not save: ' .. tostring(said)
    console.print('[mines] ' .. help)

    return false
  end

  return true
end

local function load()
  local text = fs.read(fs.data(FileName))
  if text == nil then return false end

  local lines = {}
  for line in (text .. '\n'):gmatch('([^\n]*)\n') do lines[#lines + 1] = line end
  if #lines < 6 then return false end
  if tonumber(lines[1]) ~= N then return false end
  if tonumber(lines[2]) ~= Mines then return false end

  local cells, opens, flags = lines[3], lines[4], lines[5]
  if (#cells ~= N * N) or (#opens ~= N * N) or (#flags ~= N * N) then return false end

  for i = 1, N * N do
    mine[i] = (cells:sub(i, i) == '1')
    open[i] = (opens:sub(i, i) == '1')
    flag[i] = (flags:sub(i, i) == '1')
  end

  placed = (lines[6] == '1')
  over = (lines[7] == '1')
  won = (lines[8] == '1')
  left = Mines
  for i = 1, N * N do
    if flag[i] then left = left - 1 end
  end

  return true
end

-- ---------------------------------------------------------------- the page

local function footerText()
  if help ~= '' then return help end
  if over and won then return 'Clear! Tap the arrow.' end
  if over then return 'Boom - tap the arrow.' end
  if flagMode then return 'Tap a cell to flag it.' end

  return 'Tap to open, hold to flag.'
end

local function paintCell(i, mode)
  local row = math.floor((i - 1) / N) + 1
  local col = (i - 1) % N + 1
  local x = boardX + (col - 1) * (cell + Gap)
  local y = boardY + (row - 1) * (cell + Gap)
  local cx = x + math.floor(cell / 2)
  local cy = y + math.floor(cell / 2)

  if open[i] then
    ui.rect(x, y, cell, cell, OpenFill, mode)
    ui.frame(x, y, cell, cell, OpenEdge, mode)

    if mine[i] then
      -- **La mina, dibuixada i no escrita**: a black square with four ticks, which at fifty pixels is a mine and not a
      -- blot. The same mark the paper's own minesweeper used (docs/45).
      ui.rect(cx - 7, cy - 7, 14, 14, 0, mode)
      ui.rect(cx - 1, cy - 13, 2, 4, 0, mode)
      ui.rect(cx - 1, cy + 9, 2, 4, 0, mode)
      ui.rect(cx - 13, cy - 1, 4, 2, 0, mode)
      ui.rect(cx + 9, cy - 1, 4, 2, 0, mode)
      return
    end

    local count = aroundOf(i)
    if count > 0 then
      local words = tostring(count)
      local tw = ui.text_width(words, kBody)
      local lh = ui.line_height(kBody)
      ui.text(x + math.floor((cell - tw) / 2), y + math.floor((cell - lh) / 2), words, kBody,
        NumberInk[count] or 0, mode)
    end

    return
  end

  -- **Una casella tancada és la que té relleu**: light grey with a darker frame, and a flag on it when somebody says there
  -- is a mine (docs/92).
  ui.rect(x, y, cell, cell, HiddenFill, mode)
  ui.frame(x, y, cell, cell, HiddenEdge, mode)

  if flag[i] then
    -- **La bandera són cinc rectangles i cap dibuix**: the pole, the cloth in three steps and the foot, in the ink of the
    -- page, which is what a person reads as «here» on a grey cell.
    ui.rect(cx - 2, cy - 11, 2, 22, 0, mode)
    ui.rect(cx, cy - 11, 10, 3, 0, mode)
    ui.rect(cx, cy - 8, 7, 3, 0, mode)
    ui.rect(cx, cy - 5, 4, 3, 0, mode)
    ui.rect(cx - 7, cy + 11, 14, 2, 0, mode)
  end
end

local function paintBoard(mode)
  for i = 1, N * N do paintCell(i, mode) end
end

local function paintStrip()
  if the_left ~= nil then the_left.text = 'Mines ' .. tostring(left) end
  if the_flag ~= nil then
    the_flag.checked = flagMode
    the_flag:invalidate()
  end
  if the_button ~= nil then the_button.icon = 'reboot' end
end

local function paintFooter()
  if the_foot ~= nil then the_foot.text = footerText() end
end

-- **The ink of one box is waited before the next is asked for** (`ui.idle`, docs/30): a box that arrives while the glass
-- still moves the one before is refused and stays owed, and a queue of refused boxes is a page painted in one pass.
local function waitForTheInk()
  for _ = 1, 24 do
    if ui.idle() then return end
    sys.delay(40):await()
  end
end

-- **THE PAGE IS BUILT FROM SCRATCH IN ONE PLACE**: the paper, the field, the strip and the line, and the system asks every
-- control it covered to draw again when the paper is laid, which is what brings the switch, the counter and the button
-- back when the view changes (docs/30, docs/50).
local function paintPage()
  ui.clear(15)
  ui.rect(0, 0, W, H, 15, kPageMode)
  paintBoard(kPageMode)
  paintStrip()
  paintFooter()
  ui.present(kPageMode)
  waitForTheInk()

  lastFooter = footerText()
  lastLeft = left
  dirty = {}
end

-- **What a move changed, and nothing else**: the cells it touched, in one present.
local function paintDirty()
  if #dirty == 0 then return end

  local done = {}
  for _, i in ipairs(dirty) do
    if not done[i] then
      done[i] = true
      paintCell(i, kCellMode)
    end
  end

  dirty = {}
  ui.present(kCellMode)
  waitForTheInk()
end

local function measure(w, h)
  local availW = w - 2 * Margin
  local availH = h - Strip - Footer - 16
  local side = (availW < availH) and availW or availH

  cell = math.floor((side - (N - 1) * Gap) / N)
  boardW = N * cell + (N - 1) * Gap
  boardX = math.floor((w - boardW) / 2)
  boardY = Strip + math.max(0, math.floor((availH - boardW) / 2))

  -- **El botó de la partida nova és una marca i no una paraula** (0.192.26, l'amo: *«Substitueix els botons de New i Pause per icones
  -- corresponents»*): `reboot`, el cercle que torna a començar, en un quadrat de la mateixa alçada que els altres botons del sistema,
  -- i el comptador de mines es queda amb la franja que la paraula es menjava (docs/30, docs/45).
  btnW = BtnH + 2

  btnX = boardX + boardW - btnW
  btnY = 6

  flagW = SwitchBox + ui.text_width('Flag', kSmall) + 16
  flagH = SwitchH
  flagX = btnX - 8 - flagW
  if flagX < Margin then flagX = Margin end
  flagY = 10

  if the_button == nil then
    the_button = ui.button(btnW, BtnH)
    if the_button ~= nil then
      the_button.focusable = false
      the_button.scale = kSmall
      the_button.icon = 'reboot'
    end
  else
    the_button:resize(btnW, BtnH)
  end

  if the_button ~= nil then the_button:place(btnX, btnY) end

  if the_flag == nil then
    the_flag = ui.checkbox(flagW, flagH)
    if the_flag ~= nil then
      the_flag.focusable = false
      the_flag.checked = flagMode
    end
  else
    the_flag:resize(flagW, flagH)
  end

  if the_flag ~= nil then
    the_flag:place(flagX, flagY)
    local room = flagW - SwitchBox - 8
    if room < 20 then room = 20 end
    if the_flag_words == nil then
      the_flag_words = ui.label(room, flagH)
      if the_flag_words ~= nil then
        the_flag_words.text = 'Flag'
        the_flag_words.scale = kSmall
        the_flag_words.valign = 'middle'
        the_flag:add(the_flag_words)
      end
    else
      the_flag_words:resize(room, flagH)
    end
    -- **El fill d'un control es col·loca DINS del control** (docs/30): `place` d'un fill compta des del marc del pare, i
    -- posar-hi la x de la pàgina el treia fora i el tallava —la paraula del commutador no es veia—.
    if the_flag_words ~= nil then the_flag_words:place(SwitchBox + 4, 0) end
  end

  local counter_w = flagX - Margin - 8
  if counter_w < 40 then counter_w = 40 end

  if the_left == nil then
    the_left = ui.label(counter_w, ui.line_height(kBody))
    if the_left ~= nil then
      the_left.scale = kBody
      the_left.align = 'left'
      the_left.valign = 'middle'
    end
  else
    the_left:resize(counter_w, ui.line_height(kBody))
  end

  if the_left ~= nil then the_left:place(Margin, 6) end

  -- **La línia del peu té tota l'amplada de la pàgina i no la del tauler**: una frase que no hi cap es queda amb punts
  -- suspensius, i el que aquesta línia diu és l'única cosa que explica el joc (docs/30).
  local foot_w = w - 2 * Margin
  if the_foot == nil then
    the_foot = ui.label(foot_w, 30)
    if the_foot ~= nil then
      the_foot.scale = kSmall
      the_foot.align = 'left'
    end
  else
    the_foot:resize(foot_w, 30)
  end

  if the_foot ~= nil then the_foot:place(Margin, boardY + boardW + 8) end
end

-- ---------------------------------------------------------------- the finger

local function cellAt(x, y)
  if (x < boardX) or (y < boardY) or (x >= boardX + boardW) or (y >= boardY + boardW) then return nil end

  local col = math.floor((x - boardX) / (cell + Gap)) + 1
  local row = math.floor((y - boardY) / (cell + Gap)) + 1
  if (col < 1) or (col > N) or (row < 1) or (row > N) then return nil end

  -- **The air between two cells is not a cell**: a tap in the four pixels of the gap is a tap nobody meant.
  local inX = (x - boardX) - (col - 1) * (cell + Gap)
  local inY = (y - boardY) - (row - 1) * (cell + Gap)
  if (inX >= cell) or (inY >= cell) then return nil end

  return (row - 1) * N + col
end

local function tapAt(x, y)
  -- **Els dos controls es miren aquí i no al sistema** (they are `focusable = false`, so the finger arrives at this file and
  -- not at them) and that is what leaves the centre of the dial to the system —the three presses that close an app—.
  if (x >= btnX) and (x < btnX + btnW) and (y >= btnY) and (y < btnY + BtnH) then
    newGame()
    save()
    paintPage()

    return true
  end

  if (x >= flagX) and (x < flagX + flagW) and (y >= flagY) and (y < flagY + flagH) then
    flagMode = not flagMode
    save_state()
    paintStrip()
    ui.present(kCellMode)
    return true
  end

  local i = cellAt(x, y)
  if i == nil then return false end

  if flagMode then
    if toggleFlag(i) then save() end
    return true
  end

  -- **Tocar un número obert és obrir el que li falta**: the cell is open already and the tap means its neighbours.
  if open[i] then
    if chord(i) then save() end
    return true
  end

  if openCell(i) then save() end

  return true
end

-- **A long press is a flag even with the switch off**: the finger that stays a second on a cell is saying «here» and
-- nothing else, and asking a person to find a switch first is a page that does not read the hand (docs/30).
local function holdAt(x, y)
  local i = cellAt(x, y)
  if i == nil then return false end
  if toggleFlag(i) then
    save()
    return true
  end

  return false
end

-- ---------------------------------------------------------------- the app

local caps = sys.caps()
if (caps == nil) or not caps.display then
  console.print('mines: this board has no glass')
  return
end

-- **Els tres tocs del vidre són d'aquest joc i no del sistema** (0.190.254, l'amo: *«Desactiva els 3 tocs sobre el Tetris, l'Snake
-- i qualsevol joc que no el tingui desactivat»*). Un joc llegeix cada toc, o sigui que el tercer no pot ser una ordre del sistema
-- que tanqui la partida al mig; el gest queda a la cua i aquest bucle el deixa caure, perquè només mira `tap` i `drag`. **I tornar
-- enrere no es perd**: les tres premsades de la tecla del mig són un altre camí i no les toca ningú, i quan l'app s'acaba el
-- sistema recupera els tres tocs sol (docs/30, docs/46).
if (touch.triple_tap_close ~= nil) then touch.triple_tap_close(false) end

W, H = ui.caps().width, ui.caps().height

-- **La forma de la vista, abans de col·locar res** (docs/30): the switch is read here and the page is built with it, and a
-- block of a shape this build does not know is thrown away.
local shape = load_state()
if (shape ~= nil) and (shape.flag ~= nil) then flagMode = (shape.flag == '1') end

-- **The game comes back where it was**, and a board without a save is a new game —not an error—.
if not load() then newGame() end

measure(W, H)

-- **`run minesweeper keys` prints the geometry** so a test asks instead of guessing a corner, and `full` is a whole field
-- played to the end, to look at a game without playing one (docs/45).
local asked = (app.args ~= nil) and tostring(app.args.path or '') or ''

if asked == 'keys' then
  console.print(string.format('[mines] cell %d, board %d,%d %dx%d, new %d,%d %dx%d, flag %d,%d %dx%d',
    cell, boardX, boardY, boardW, boardW, btnX, btnY, btnW, BtnH, flagX, flagY, flagW, flagH))
end

-- **`full` és un camp acabat**: the mines every eighth cell —ten of them, which is the field of this game— and every safe
-- cell opened, which is the win: a page to look at without playing a game, and the only way a test can see the numbers, the
-- flags and the mines of a whole field (docs/45).
if asked == 'full' then
  newGame()
  for i = 1, N * N do
    if (i % 8) == 0 then mine[i] = true end
  end
  placed = true
  for i = 1, N * N do
    if not mine[i] then openCell(i) end
  end
  console.print('[mines] a whole field, over=' .. (over and 1 or 0) .. ' won=' .. (won and 1 or 0))
end

paintPage()

function on_pause() save(); save_state() end
function on_save() save(); save_state() end
function on_close() save(); save_state() end

while true do
  local view = ui.caps()
  if (view.width ~= W) or (view.height ~= H) then
    W, H = view.width, view.height
    measure(W, H)
    paintPage()
  end

  -- **Els gestos es prenen i la cua es buida**: a gesture nobody takes is a queue that grows, and the finger of this page is
  -- a tap and a long press (docs/30).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'tap' then
      tapAt(gesture.x or 0, gesture.y or 0)
    elseif gesture.kind == 'long' then
      holdAt(gesture.x or 0, gesture.y or 0)
    elseif gesture.kind == 'drag' then
      if (math.abs(gesture.dx or 0) < kLongPx) and (math.abs(gesture.dy or 0) < kLongPx) then
        tapAt(gesture.x or 0, gesture.y or 0)
      end
    end

    gesture = touch.gesture()
  end

  paintDirty()

  -- **El comptador, quan ha canviat**: the strip is a label and not a cell, o sigui que no el torna a pintar `paintDirty`
  -- —i un comptador que no baixa quan es posa una bandera és una pàgina que diu una altra cosa que el camp—.
  if left ~= lastLeft then
    paintStrip()
    ui.present(kCellMode)
    waitForTheInk()
    lastLeft = left
  end

  -- **I la línia del peu, quan diu una altra cosa**: one box and one pass, in the quality that cleans.
  local foot = footerText()
  if foot ~= lastFooter then
    paintFooter()
    ui.present(kClearMode)
    waitForTheInk()
    lastFooter = foot
  end

  sys.delay(20):await()
end
