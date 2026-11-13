-- 2048: the tiles, on electronic ink.
--
-- Ported from the paper project (`data/app/2048/pos_launch.lua`, version 1.1.0) with **the screen and the
-- behaviour whole** and the original's numbers (docs/45). A board of 4 x 4, a finger that swipes, and the rule
-- everybody knows: equal tiles that meet become one of double the value, and after every move that CHANGES
-- something a new tile appears in one of the empty cells. Reaching 2048 is the goal, running out of moves is the
-- end.
--
-- **THE VALUE IS WHAT DISTINGUISHES A TILE, AND IT IS PAINTED WITH GREYS.** On a screen with sixteen greys and no
-- colour the tile of a 1024 cannot be told from the tile of a 2 by its hue, so every step of the ladder gets a
-- STEP OF DARKNESS: the small tiles are paper with a frame, the middle ones fill up, and 2048 and up are black
-- with white numbers. That is also what makes the board readable at a glance after a swipe.
--
-- **AND THE APP PAINTS ONLY WHAT CHANGED, AND SAYS WHAT IT IS** (the design the paper's owner dictated on 26/10,
-- kept here because it is the whole of the cost of a move): a tile that is another number is painted on its own
-- and in sixteen greys, the score digits and the button travel in two inks, and the line at the bottom only when
-- its message changes. The paper of the board is never painted again if the luck holds. In this system the
-- quality is an argument of the painting call, so `markChanges` is the same rule with one name less (docs/30).
--
-- **THE MOVEMENT ITSELF IS THE FINGER**, and left and right are ALSO on the two side keys: the panel has three
-- keys and the game has four directions, so the swipes are the real input and the keys cost three lines. The
-- CENTRE key is left alone on purpose — registering it would take the three presses that close an app away from
-- the system (docs/30).
--
-- **THE PART GAME IS KEPT.** Every move that changes something is written as the sixteen tiles and the score,
-- and opening the app again finds it. A move that changes NOTHING is not written, which is the same rule as «it
-- does not repaint»: a board that did not move is a board that was not touched.

local N = 4                     -- the board is 4 x 4, and that is the game
local Goal = 2048               -- what this game is about
local FileName = 'game.txt'     -- the save, inside this app's own folder (`fs.data`)
local MaxExp = 17               -- 2^17 = the ceiling of a tile that is read

local Gap = 8                   -- between tiles, in pixels
local Strip = 78                -- the top strip: the score and the New button
local BtnH = 63                 -- the button's height, which is the minimum a finger can hit
-- **The room a button of this system leaves its own word is sixteen pixels on each side** —`padX` of
-- `paint_button`, the same measure for the word and for a tag—, and not the fourteen of the paper's button: measured
-- with fourteen, «Sure?» came out four pixels short and the system painted «Sur…» inside the frame (found on the
-- board, 11/11). The button is the original's plus those four pixels and not a number picked by eye.
local BtnPad = 16
local BtnMinW = 84              -- never narrower than it was, even with a word that would fit in less
local Footer = 63               -- the bottom line: what the game is saying

-- **The ladder of darkness, by exponent**: 1 is a tile of 2 and 11 is a tile of 2048. Above the last step
-- everything is black, because a screen has no more ink to give and a 4096 is read by its number anyway.
local TileGrey = { 14, 13, 12, 11, 9, 8, 6, 5, 4, 3, 2, 0, 0, 0, 0, 0, 0 }

local FourIn = 10               -- one spawn in ten is a four (the game everybody plays does it like this)

-- **The sizes of the paper's face, in pixels of line** (docs/45): small 33, body 43 and title 57. Asking for the
-- size and not the step keeps the page the same physical size on both projects (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  return math.max(1, math.min(16, math.floor(line_px / step + 0.5)))
end

local kSmall = scale_for(33)
local kBody = scale_for(43)
local kTitle = scale_for(57)

-- **How far a finger has to slide for a drag to be a swipe.** The system's gesture is a drag with `dx` and `dy`
-- and the direction is the app's to read; a tap moves no more than twenty pixels, so thirty is a slide and not a
-- shaky finger (docs/30).
local kSwipePx = 30

-- The qualities this app asks for: a tile is sixteen greys and the digits two inks, which is the design of the
-- original and the difference between 450 ms and 120 ms per paint (docs/50).
local kTileMode = 'gld16'
local kTextMode = 'gld16'
local kPageMode = 'gl16'

local board = {}                -- N*N exponents: 0 is an empty cell
local before = {}               -- the board as it is on the glass, to see what changed
local score = 0
local over = false              -- no move is possible any more
local won = false               -- the goal was reached, once
local sure = false              -- the New button is asking «really?»
local canSave = true            -- false when the save cannot be trusted
local why = ''                  -- and the reason, which is said on screen

local cell, boardW, boardX, boardY   -- the geometry, measured for this view
local btnX, btnY, btnW               -- and the button, measured from its own word
local the_button = nil               -- the New of the system, placed and told what to say
local the_score = nil                -- the score, a label of the system (opaque: the old number goes with its box)
local the_foot = nil                 -- and the line at the bottom, the same

-- ------------------------------------------------------------------------- the board

local function index(row, col) return (row - 1) * N + col end

-- The value of an exponent: 0 is an empty cell and not a tile of 1.
local function value(exp) return (exp == 0) and 0 or (1 << exp) end

local spots = {}                -- reused: a new table per move is rubbish

local function emptyCells(into)
  local n = 0

  for i = 1, N * N do
    if board[i] == 0 then
      n = n + 1
      into[n] = i
    end
  end

  return n
end

-- **A NEW TILE, in one of the empty cells. THE SEQUENCE IS THE GAME'S**, so that a test can fix it with a seed
-- and read the same board twice. `alwaysTwo` is for the two tiles a game starts with, which are twos and not one
-- two and one four (the paper's 26/10): the four-in-ten draw starts with the first move.
local function spawn(alwaysTwo)
  local n = emptyCells(spots)
  if n == 0 then return false end

  local at = spots[math.random(n)]

  if alwaysTwo then
    board[at] = 1
  else
    board[at] = (math.random(FourIn) == 1) and 2 or 1
  end

  return true
end

-- **THE SUM OF THE BOARD**, which is the number that says whether a move was legal: tiles merge and the sum only
-- grows by what the spawn adds, so a sum that dropped would be a tile that vanished.
local function total()
  local sum = 0

  for i = 1, N * N do sum = sum + value(board[i]) end

  return sum
end

-- One line of the board, in the direction of the move: everything slides, equal neighbours merge ONCE, and what
-- is left is packed at the side the finger went to.
local function slideLine(line, earn)
  local packed = {}
  local n = 0

  for i = 1, #line do
    if line[i] ~= 0 then
      n = n + 1
      packed[n] = line[i]
    end
  end

  local out = {}
  local o = 0
  local i = 1

  while i <= n do
    -- TWO EQUAL TILES THAT MEET BECOME ONE, and the one that comes out of a merge cannot merge again in the same
    -- move: that is the rule of the game, and without it four twos would make a single eight.
    if (i < n) and (packed[i] == packed[i + 1]) then
      o = o + 1
      out[o] = packed[i] + 1
      earn = earn + value(out[o])
      i = i + 2
    else
      o = o + 1
      out[o] = packed[i]
      i = i + 1
    end
  end

  while o < N do
    o = o + 1
    out[o] = 0
  end

  return out, earn
end

-- A MOVE: the four directions are the same walk with the line read another way.
local function move(dr, dc)
  local earn = 0
  local moved = false
  local line = {}
  local seen = {}

  for i = 1, N * N do seen[i] = board[i] end

  for k = 1, N do
    for j = 1, N do
      local row, col

      if dr ~= 0 then
        row = (dr > 0) and (N - j + 1) or j
        col = k
      else
        row = k
        col = (dc > 0) and (N - j + 1) or j
      end

      line[j] = board[index(row, col)]
    end

    local out, gained = slideLine(line, earn)
    earn = gained

    for j = 1, N do
      local row, col

      if dr ~= 0 then
        row = (dr > 0) and (N - j + 1) or j
        col = k
      else
        row = k
        col = (dc > 0) and (N - j + 1) or j
      end

      board[index(row, col)] = out[j]
    end
  end

  for i = 1, N * N do
    if board[i] ~= seen[i] then moved = true end
  end

  return moved, earn
end

-- Whether the game can go on: an empty cell is always a move, and two equal neighbours are one too.
local function canMove()
  for i = 1, N * N do
    if board[i] == 0 then return true end
  end

  for row = 1, N do
    for col = 1, N do
      local v = board[index(row, col)]

      if (col < N) and (board[index(row, col + 1)] == v) then return true end
      if (row < N) and (board[index(row + 1, col)] == v) then return true end
    end
  end

  return false
end

-- -------------------------------------------------------------------- the save file

-- The ground of an app is `/data/<id>`, which is this system's answer to the paper's `storage.preferred()`
-- (docs/30, docs/45). The folder can be missing on a disk that never had a save, and making it twice is not an
-- error worth a line.
local function save()
  if not canSave then return false end

  local text = ''

  for i = 1, N * N do
    if i > 1 then text = text .. ',' end
    text = text .. tostring(board[i])
  end

  text = text .. '\n' .. tostring(score) .. '\n'

  local ground = fs.data()
  if ground ~= nil then pcall(fs.mkdir, ground) end

  local written, reason = fs.write(fs.data(FileName), text)

  if written == nil then
    canSave = false
    why = 'Could not save: ' .. tostring(reason)
    console.print('[2048] ' .. why)

    return false
  end

  return true
end

local function load()
  local text, reason = fs.read(fs.data(FileName))

  if text == nil then
    -- NO SAVE IS NOT AN ERROR: a first game is a game like any other. The reason is said once, because a save
    -- that cannot be read is a game that will not come back.
    console.print('[2048] no save (' .. tostring(reason) .. '): a new game')

    return false
  end

  local tiles = {}
  local n = 0

  for piece in string.gmatch(text, '[^,\n]+') do
    n = n + 1

    if n <= N * N then
      local exp = tonumber(piece) or 0

      if exp < 0 or exp > MaxExp then exp = 0 end

      tiles[n] = exp
    elseif n == N * N + 1 then
      score = tonumber(piece) or 0
    end
  end

  if n < N * N then
    console.print('[2048] the save is short (' .. n .. ' pieces): a new game')

    return false
  end

  for i = 1, N * N do board[i] = tiles[i] end

  return true
end

-- ------------------------------------------------------------------------- the drawing

local function textOf(exp, room)
  -- The biggest scale whose number fits in the tile: a tile of 2 is read from far away and a tile of 2048 has to
  -- fit, and there is no scaler here.
  local s = tostring(value(exp))
  local sizes = { kTitle, kBody, kSmall }

  for i = 1, #sizes do
    if ui.text_width(s, sizes[i]) <= room then return s, sizes[i] end
  end

  return s, kSmall
end

-- **A tile is painted, and the quality is the tile's**: sixteen greys, which is what makes fourteen steps of
-- darkness readable and what stops the dark ones from coming out as a dither (the paper's 26/10).
local function paintTile(row, col, exp, mode)
  local x = boardX + (col - 1) * (cell + Gap)
  local y = boardY + (row - 1) * (cell + Gap)

  -- **THE BOX OF A CELL IS PAINTED WHOLE, AND A CELL THAT EMPTIES IS PAPER FIRST** (the paper's `on_draw` starts with
  -- a `draw.clear(15)` of the whole view and paints the page again, so its buffer never keeps an old tile inside a
  -- cell; here the page is painted a box at a time —it is the only way a move costs one cell and not the board— and
  -- that makes the paper of the box this file's job). Without that fill, a cell that empties keeps the ink of the tile
  -- it had inside its frame, and the next time that box travels —the next tile that lands there— **the old tile comes
  -- back out of the buffer**. That is what a board that does not refresh looks like, and it is not the panel's fault
  -- (docs/30, docs/45).
  if exp == 0 then
    -- AN EMPTY CELL IS A FRAME: the grid has to be seen before the first tile arrives, or a board of four tiles
    -- looks like a board of sixteen. And the paper under it, because a frame draws its four edges and not its middle.
    ui.rect(x, y, cell, cell, 15, mode)
    ui.frame(x, y, cell, cell, 12, mode)
    return
  end

  local grey = TileGrey[exp] or 0

  ui.rect(x, y, cell, cell, grey, mode)

  local s, scale = textOf(exp, cell - 16)
  local tw = ui.text_width(s, scale)
  local lh = ui.line_height(scale)

  -- ON A DARK TILE THE NUMBER IS WHITE, and it is not decoration: black ink on a black tile is a tile with no
  -- number on it.
  local ink = (grey <= 6) and 15 or 0

  ui.text(math.floor(x + (cell - tw) / 2), math.floor(y + (cell - lh) / 2), s, scale, ink, mode)
end

local function paintBoard(mode)
  for row = 1, N do
    for col = 1, N do paintTile(row, col, board[index(row, col)], mode) end
  end
end

local function stripText()
  return 'Score ' .. tostring(score)
end

-- **The score is a label of the system and not a line of text this file paints**, and it is opaque: changing the
-- number repaints its whole box with paper and then the digits, so the number that was there cannot stay under the
-- new one (docs/30). What the design of the original asks —the digits travel on their own and not the page— is what
-- a control gives for free: its box is the damage.
local function paintStrip()
  if the_score ~= nil then the_score.text = stripText() end

  -- **The New is a button of the system and not a frame this file paints** (the rule of the project: the control
  -- that exists is the one that is used, and the paper's hand-painted one was there because its launcher was the
  -- panel's). The system paints it, measures its word, takes the old ink away when the word changes —from «New» to
  -- «Sure?» and back— and knows how a button looks when a finger is on it (docs/30, docs/45).
  if the_button ~= nil then the_button.title = sure and 'Sure?' or 'New' end
end

-- **What the app was asked for**: `run 2048 keys` prints the geometry so a test asks instead of guessing a corner,
-- `seed=N` fixes the sequence and `full` starts from a board with no possible move —to look at the end of a game
-- without playing one— (the paper's `-seed` and `-full`, docs/45). **Declared up here and not at the end of the
-- file**: the functions below read them, and a local declared after them would be a nil global inside them.
local asked = (app.args ~= nil) and tostring(app.args.path or '') or ''

local seed = asked:match('seed=(%d+)')
if seed ~= nil then math.randomseed(tonumber(seed)) end

local function footerText()
  if why ~= '' then return why end
  if over then return 'No moves left - the game is over.' end
  if won then return '2048! Keep going, or tap New.' end

  return 'Swipe to move.'
end

local function paintFooter()
  -- **The line at the bottom is a label too**: it is one line of words over paper, and the system's label is opaque —
  -- the message that was there goes with the box it is painted on.
  if the_foot ~= nil then the_foot.text = footerText() end
end

-- **THE DESIGN OF THE REFRESH** (the paper's 26/10, and the whole of the cost of a move): what changed in value
-- is painted on its own and in sixteen greys, the score digits and the button in two inks, and the line at the
-- bottom only when its message is another one. `before` is the board that is on the glass.
local function markChanges(footerChanged)
  -- **A CELL THAT CHANGED IS PAINTED WHOLE** —paper, then the tile or the frame— and that is the mark: the box is the
  -- damage and the mode says what the glass has to pay for it. A cell that did not change is not painted, not marked
  -- and not sent: the paper of the board never travels again after the first paint (docs/30, docs/45).
  for row = 1, N do
    for col = 1, N do
      local i = index(row, col)

      if board[i] ~= before[i] then paintTile(row, col, board[i], kTileMode) end
    end
  end

  paintStrip()

  if footerChanged then paintFooter() end

  ui.present()
end

local function copyBoard(into)
  for i = 1, N * N do into[i] = board[i] end
end

local function say()
  console.print('[2048] board: ' .. table.concat(board, ',') ..
    ' score=' .. score ..
    ' over=' .. (over and 1 or 0) ..
    ' sum=' .. total())
end

-- A MOVE AS THE GAME SEES IT: if nothing moved, nothing happens —not a save, not a repaint, not a log line—. The
-- answer tells the caller whether anything was painted, which is what a swipe that goes into a wall costs.
local function play(dr, dc)
  if over then return false end

  copyBoard(before)

  local wasOver, wasWon = over, won
  local moved, earn = move(dr, dc)

  if not moved then return false end

  score = score + earn

  spawn()

  if not won then
    for i = 1, N * N do
      if board[i] >= 11 then won = true end   -- 11 is a tile of 2048
    end
  end

  over = not canMove()

  save()
  say()

  if over then console.print('[2048] no moves left, score=' .. score) end

  -- ONLY WHAT CHANGED, and at the quality it deserves: the tiles that are another number, the digits of the score
  -- and the button, and the foot when its message is another one. Nothing else of the screen travels.
  markChanges((over ~= wasOver) or (won ~= wasWon))

  return true
end

-- A NEW GAME: the score digits, the button —so it says «New» again— and the cells that are another number.
local function newGame()
  copyBoard(before)

  local wasOver, wasWon = over, won

  for i = 1, N * N do board[i] = 0 end

  score, over, won, sure = 0, false, false, false

  -- TWO TWOS, ALWAYS: it is the only place of the game that does not throw the four's die.
  spawn(true)
  spawn(true)

  save()
  say()
  markChanges(wasOver or wasWon)
end

-- --------------------------------------------------------------------------- the geometry

local function measure(w, h)
  local availW = w - 24
  local availH = h - Strip - Footer - 16
  local side = (availW < availH) and availW or availH

  cell = math.floor((side - (N - 1) * Gap) / N)
  boardW = N * cell + (N - 1) * Gap
  boardX = math.floor((w - boardW) / 2)
  boardY = Strip + math.floor((availH - boardW) / 2)

  if boardY < Strip then boardY = Strip end

  -- THE BUTTON IS MEASURED FROM ITS WORD and sits where it sat: its right edge is the board's right edge, the same
  -- 12 px of paper the score has on the left. What changes is only how wide it is, which is what makes «Sure?»
  -- fit (the paper's 26/10). **And two pixels of air on top**, because the word is measured here and cut inside the
  -- control with the same arithmetic: a difference of one pixel is a word with three dots at the end.
  btnW = ui.text_width('Sure?', kSmall) + 2 * BtnPad + 2
  if btnW < BtnMinW then btnW = BtnMinW end

  btnX = boardX + boardW - btnW
  btnY = 8

  -- **The button itself**: a control of the system, told where it sits and how wide it is, and **out of the dial**
  -- (`focusable = false`) because the centre of this panel is the way out of an app and this file reads the press
  -- itself —it is the game that decides between «New» and «Sure?»— (docs/30).
  if the_button == nil then
    the_button = ui.button(btnW, BtnH)
    if the_button ~= nil then
      the_button.focusable = false
      the_button.scale = kSmall
    end
  else
    the_button:resize(btnW, BtnH)
  end

  if the_button ~= nil then the_button:place(btnX, btnY) end

  -- **The two labels of the page**: the score on the left of the strip, in the body's size, and the line of the
  -- bottom in the small one. A label is opaque —its box is paper and then the words— so a number that changes takes
  -- the one it replaces with it, and its box is what the system paints (docs/30).
  local strip_w = btnX - 12
  if strip_w < 40 then strip_w = 40 end

  if the_score == nil then
    the_score = ui.label(strip_w, BtnH)
    if the_score ~= nil then
      the_score.scale = kBody
      the_score.align = 'left'
    end
  else
    the_score:resize(strip_w, BtnH)
  end

  if the_score ~= nil then the_score:place(12, 18) end

  if the_foot == nil then
    the_foot = ui.label(boardX + boardW - 24, 30)
    if the_foot ~= nil then
      the_foot.scale = kSmall
      the_foot.align = 'left'
    end
  else
    the_foot:resize(boardX + boardW - 24, 30)
  end

  if the_foot ~= nil then the_foot:place(12, boardY + boardW + 14) end
end

-- ------------------------------------------------------------------------------- the input

local function tapAt(x, y)
  if (x >= btnX) and (x < btnX + btnW) and (y >= btnY) and (y < btnY + BtnH) then
    if sure then
      newGame()
    else
      -- The first tap asks. A game thrown away by accident is a game nobody gets back, so the word changes from
      -- «New» to «Sure?» and the button travels on its own, in two inks.
      sure = true
      paintStrip()
      ui.present()
    end

    return
  end

  if sure then
    sure = false
    paintStrip()
    ui.present()
  end
end

-- **A swipe is a drag the app reads**: the system says `drag` with `dx` and `dy` —two fingers that move together
-- are one pan and the point is the middle one— and the direction belongs to the game, which is the only one that
-- knows which way a board can be pushed (docs/30).
local function swipe(dx, dy)
  if math.abs(dx) < kSwipePx and math.abs(dy) < kSwipePx then return false end

  if math.abs(dx) >= math.abs(dy) then
    return play(0, (dx > 0) and 1 or -1)
  end

  return play((dy > 0) and 1 or -1, 0)
end

-- **LEFT AND RIGHT ARE ALSO ON THE TWO SIDE KEYS** (the paper's design): the panel has three keys and the game
-- has four directions, so the finger is the real input. **Les tecles es demanen a la cua** (0.181.2): `button.subscribe` is
-- the queue of the system and the game hears the side keys **even though the page has a button** —the dial used to take them
-- before the hook could see them—; the centre is left to the system on purpose, which is what the button of a new game is
-- for (docs/30). The place in the queue is given back by the system when the app ends.
button.subscribe(function(event)
  if event.kind ~= 'press' then return end

  -- **La tecla lateral es diu `up` i `down` a la Paper** i `left`/`right` en una placa que les tingui al costat: el nom és
  -- el lloc on és la tecla i no el número, i el joc dona el mateix a les dues parelles —la de dalt és la columna d'anar
  -- enrere i la de baix la d'anar endavant, el mateix ordre que el dial— (docs/30, docs/50).
  if event.spot == 'left' or event.spot == 'up' then
    play(0, -1)
  elseif event.spot == 'right' or event.spot == 'down' then
    play(0, 1)
  end
end)

-- --------------------------------------------------------------------------------- the app

-- **A board with no glass says so and leaves**: `ui.caps()` is `nil` there, and an app that indexes it is a Lua error
-- in the console instead of a sentence a person can read (the same door `home_eink` uses, docs/40).
local caps = sys.caps()
if not caps.display then
  console.print('2048: this board has no glass')
  return
end

W, H = ui.caps().width, ui.caps().height
measure(W, H)

if asked == 'keys' then
  console.print(string.format('[2048] cell %d, board %d,%d %dx%d, new button %d,%d %dx%d',
    cell, boardX, boardY, boardW, boardW, btnX, btnY, btnW, BtnH))
end

if asked == 'full' then
  -- A BOARD WITH NO MOVE IN IT: the end of a game, to look at it.
  local pattern = { 1, 2, 1, 2, 2, 1, 2, 1, 1, 2, 1, 2, 2, 1, 2, 1 }

  for i = 1, N * N do board[i] = pattern[i] end

  score, over, won, why = 8192, true, true, ''
  copyBoard(before)
  say()
  console.print('[2048] no moves left, score=' .. score)
elseif not load() then
  newGame()
else
  over = not canMove()
  say()
end

-- The whole page at once and with one quality: it is the first paint of an app and the glass has to be clean
-- (docs/50). From here on only the damage of a move travels.
ui.clear(15)
paintBoard()
paintStrip()
paintFooter()
ui.present(kPageMode)

local lastFooter = footerText()

while true do
  -- **The room of the view is what the page is measured from**: the system can give the app another view —the whole
  -- glass, a turn, a smaller bar— and the page is built again from the new numbers (docs/40).
  local view = ui.caps()
  if view.width ~= W or view.height ~= H then
    W, H = view.width, view.height
    measure(W, H)
    ui.clear(15)
    paintBoard()
    paintStrip()
    paintFooter()
    ui.present(kPageMode)
    lastFooter = footerText()
  end

  -- **The gestures of the system are taken and not left in the queue**: a drag is a swipe, a tap is the button, and
  -- anything else is read and dropped, because a queue nobody empties is a queue that grows (docs/30).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'drag' then
      swipe(gesture.dx or 0, gesture.dy or 0)
    elseif gesture.kind == 'tap' then
      tapAt(gesture.x or 0, gesture.y or 0)
    end

    gesture = touch.gesture()
  end

  -- A move may have changed the line of the foot without painting it: it is painted here, once, when its message
  -- is another one, which is the rule the design of the refresh asks for.
  local foot = footerText()
  if foot ~= lastFooter then
    paintFooter()
    ui.present()
    lastFooter = foot
  end

  sys.delay(20):await()
end
