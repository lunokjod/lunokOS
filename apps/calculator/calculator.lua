-- calculator: a pocket calculator on the glass, the port of the paper project's calculator (0.121.0):
-- the same keypad, the same numbers and the same rules, one for one.
--
-- **IT IS PAINTED BY HAND AND THAT IS NOT A STYLE CHOICE.** The controls of this system are a tree where
-- every control has a buffer, and a pocket calculator asks for nineteen keys: a keypad of widgets would
-- cost the app's memory once per key and would turn the page into a form. Painting costs the canvas the
-- page already has, and it is what the original did, for the same reason.
--
-- **THE SAME RECTANGLE PAINTS A KEY AND TAKES THE FINGER.** `build_layout` measures the keypad once and
-- both `draw_page` and `key_at` read the rectangles from there. A key painted with one rectangle and
-- touched with another only works where nobody is looking.
--
-- **A KEY DOES NOT REPAINT THE PAGE.** A press changes the state and paints the display box alone, with
-- the quality of a page of text (`a2`), so a digit costs one box and not nineteen keys. On e-ink that is
-- the difference between a calculator and a screen that flashes at every digit (docs/50).
--
-- **THE FINGER IS POLLED AND NOT TAKEN FROM A GESTURE.** The system says a `tap` 350 ms after the finger
-- leaves and merges two taps inside that window into one `double`, which on a calculator is a digit that
-- arrives late and two keys that become one. So the app reads `touch.read()` —the frame of the glass, in
-- the coordinates of its view— and presses on the first frame of a contact (docs/30, docs/50).
--
-- **AND DIVIDING BY ZERO HAS NO ANSWER**: the glass says so with a word and not with a number that is not
-- one (`inf` is a value of Lua and not a result a person can read).
--
-- **THE LOG REMEMBERS EVERY SUM**: each `=` prints one line, which is what a test reads to know that the
-- arithmetic and the keys came out right together without looking at the pixels. And `run calculator
-- keys` prints where the display and every key are, so a test does not have to know the layout to press
-- one — the same idea as the original's `-keys`, said the way this system starts an app with a word
-- (docs/30, docs/85).

-- **THIS PAGE HAS A `state` SESSION, AND IT IS WHAT BRINGS THE NUMBER AND THE HISTORY BACK** (0.189.165, the owner: *«la
-- calculador ha de tenir també session state, per el número de la pantalla i l'historial d'operacions (que per ara no es
-- mostra)»*). The 0.189.52 had decided the opposite —the page carries no control with a ring, and *«el número és el que la
-- persona fa ara i no la forma de la vista»*—, and what changed is that a calculator opened again on a zero with no last sum
-- is not the calculator the person left. The block is **the shape of the view and nothing else**: the number on the screen
-- —the one being typed or the one held—, the operation waiting for its second operand, and **the history of the operations
-- that were made**, que és el camí que hi ha portat. **A history is not a document** —a document is what has to survive
-- `sessions destroy`, and that one goes to `fs.data`—, so the last twelve are what the block keeps (docs/30 § la sessió
-- `state`, docs/45 § 5 bis).
local caps = sys.caps()
if not caps.display or not caps.touch then
  console.print('this board needs a glass and a touch panel for this')
  return
end

-- The two qualities this page uses: the whole page carries one grey for the operators, and the display
-- box is a page of text, black and white and nothing between them (docs/50).
local kPageMode = 'gl16'
local kKeyMode = 'a2'

-- The most digits a number may hold: fourteen figures do not fit the display at the size of the number,
-- and past a dozen nobody is reading them any more.
local MaxDigits = 12

-- **The four sizes of the paper project, in pixels of line** (`lib/gfx/fonts/fonts.h` of PaperOS: `small`
-- 33, `body` 43, `title` 57 and `big` 82). They are asked **in pixels and not in steps** because a step of
-- this system's face is eleven pixels with the bold face of the disk and seven with the table of the flash:
-- asking for the size keeps the page the same size on both, and it is what `ui.line_height` is for
-- (0.121.0, docs/30). The port was written in steps first and the text came out **noticeably smaller**
-- than the original —the keys at 33 px of line instead of 57, the number at 44 instead of 82—.
local kSmallLine, kBodyLine, kTitleLine, kBigLine = 33, 43, 57, 82

local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

local kSmall = scale_for(kSmallLine)
local kBody = scale_for(kBodyLine)
local kTitle = scale_for(kTitleLine)
local kBig = scale_for(kBigLine)

-- The ladder of the number, from the biggest to the smallest, which is the order the original stepped down
-- in: `big`, `title`, `body`, `small`.
local Ladder = {kBig, kTitle, kBody, kSmall}

-- What the machine is doing: the left number, the operation waiting for its second operand, the digits
-- being typed, and whether the next digit opens a new number (after an operation, or after equals).
local acc = nil
local pending = nil
local entry = ''      -- exactly what the finger has typed, as it is shown
local fresh = true
local failed = false  -- dividing by zero: the next key clears the way

-- **The history of the operations**, one line per `=` in the same words the console gets (`2 + 3 = 5`), which is what the
-- page will show over the number the day it has the room —the owner asked for it on the 16th: *«Vull que es vegi l'historial
-- de les operacions amb els valors»*— (docs/30, docs/45).
local history = {}

-- ------------------------------------------------------------------ the state of the page (docs/30 § state)

-- **The shape that is written** (0.189.165): the first line is the version of the shape and the rest are the fields. A block
-- of another version is thrown away and not migrated, and a field that is not what it has to be is not used (docs/30,
-- docs/45).
local kLayout = 'v1'
local kStateBytes = 1024
-- **The last twelve operations**: the block is a kilobyte and the number has to fit in it with them, so the oldest ones are
-- the first to go (docs/30).
local kMostHistory = 12
local state_block = nil

local function the_state()
  -- **Un bloc que el sistema s'ha quedat no es pot fer servir** (0.192.55): the session blocks are the first thing the allocator
  -- gives back when the memory is short, and every call on one that is gone raises `the session memory was given back`, which ends
  -- the app. It is asked again —that is what the contract says an app must do— and if there is no room the page is built as if it
  -- were the first time (docs/30, docs/75).
  if state_block ~= nil and not state_block:alive() then state_block = nil end
  if state_block == nil then
    -- **A short memory is not a failure**: `sys.session` answers `nil` when the board has no room for the block, and then
    -- the page is built as if it were the first time (docs/30).
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function save_state()
  local block = the_state()
  if block == nil then return false end

  local lines = { kLayout }
  -- **A number and not a text**: `%.17g` is the shortest text that reads back as the same number, so a sum of half a
  -- hundredth does not come back as another number (docs/30).
  if acc ~= nil then lines[#lines + 1] = string.format('acc=%.17g', acc) end
  if entry ~= '' then lines[#lines + 1] = 'entry=' .. entry end
  if pending ~= nil then lines[#lines + 1] = 'pending=' .. pending end
  lines[#lines + 1] = 'fresh=' .. (fresh and 'yes' or 'no')
  lines[#lines + 1] = 'failed=' .. (failed and 'yes' or 'no')

  -- **Una llista repetint la clau, i sense escapar res** (0.189.165): the history is one `h=` line per operation and not a
  -- `text:` value with the newlines escaped, perquè una línia d'aquesta pàgina només pot tenir xifres, els quatre signes, un
  -- punt, un espai, un `=` i l'`e` d'un exponent —cap salt de línia i cap barra invertida—, o sigui que el que es desa es
  -- llegeix tal com es va escriure (docs/30 § el bloc i el format).
  local first = #history - kMostHistory + 1
  if first < 1 then first = 1 end
  for i = first, #history do lines[#lines + 1] = 'h=' .. history[i] end

  -- **What does not fit is the oldest**, dropped from the front until the block holds the rest: the number and the operation
  -- that is waiting are what the person has to find (docs/30).
  local text = table.concat(lines, '\n')
  while #text > kStateBytes - 1 do
    local drop = 0
    for i = 1, #lines do
      if lines[i]:sub(1, 2) == 'h=' then
        drop = i
        break
      end
    end
    if drop == 0 then break end
    table.remove(lines, drop)
    text = table.concat(lines, '\n')
  end

  block:clear()
  block:write(0, text)
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end

  local body = block:read(0, kStateBytes) or ''
  -- **The block is a kilobyte and what is written is shorter**: behind it is the zero the kernel filled it with, and a line
  -- followed by zeros is not a value (docs/30).
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end

  local now = { history = {} }
  local first = true
  for one in body:gmatch('[^\n]+') do
    if first then
      first = false
      if one ~= kLayout then return nil end
    else
      local key, value = one:match('^([%w_]+)=(.*)$')
      if key == 'h' then
        if #now.history < kMostHistory then now.history[#now.history + 1] = value end
      elseif key ~= nil then
        now[key] = value
      end
    end
  end
  if first then return nil end
  return now
end

-- **What is read is looked at before it is used** (the rule of docs/30 and docs/45 §5 bis): a number that is not one, an
-- entry with something that is not a figure, an operation that is not one of the four —the page goes on as if it were the
-- first time instead of trusting a block of another shape.
local function restore()
  local was = load_state()
  if was == nil then return end

  if was.acc ~= nil then acc = was.acc:match('^%-?[%d%.eE+-]+$') ~= nil and tonumber(was.acc) or nil end
  if was.entry ~= nil and #was.entry <= MaxDigits and was.entry:match('^%-?%d*%.?%d*$') ~= nil then
    entry = was.entry
  end
  if was.pending == '+' or was.pending == '-' or was.pending == '*' or was.pending == '/' then
    pending = was.pending
  end
  fresh = (was.fresh == 'yes')
  failed = (was.failed == 'yes')
  history = was.history
end

-- The view, and the two boxes measured from it.
local W, H = 0, 0
local disp = {x = 0, y = 0, w = 0, h = 0}
local keys = {}       -- one table a key: {x, y, w, h, label, action}

-- ------------------------------------------------------------------- the pad

-- The labels, the operation and the width. `span` is how many columns a key covers: the zero is double,
-- as it is on every calculator, and that is what leaves room for the point and equals in the same row.
--
-- **The same four signs as the original** (0.158.0): the keypad of `pos_launch.lua` of PaperOS is
-- `{"C"}, {"←", "back"}, {"±", "neg"}, {"÷", "/"}` and `{"×", "*"}` in the second row, and the
-- comment of that file says why: «the glyphs are the ones the bitmap font carries (`tools/fontgen.py`): the
-- arrows, the plus-minus, the multiplication and the division signs are all there». The text of this system
-- is **one byte a character** and not UTF-8, so a sign is `string.char` and never the letter itself in this
-- file —the three Latin-1 ones are `0xD7`, `0xF7` and `0xB1`, and the arrow is `0x81`, one of the five codes
-- Windows-1252 leaves empty— (docs/30, docs/92 §34).
local kTimes, kDivide = string.char(0xD7), string.char(0xF7)
local kPlusMinus, kArrow = string.char(0xB1), string.char(0x81)

local Rows = {
  {{'C', 'clear'}, {kArrow, 'back'}, {kPlusMinus, 'neg'}, {kDivide, '/'}},
  {{'7', 'd'},     {'8', 'd'},       {'9', 'd'},          {kTimes, '*'}},
  {{'4', 'd'},     {'5', 'd'},       {'6', 'd'},          {'-', '-'}},
  {{'1', 'd'},     {'2', 'd'},       {'3', 'd'},          {'+', '+'}},
  {{'0', 'd', 2},  {'.', 'dot'},     {'=', '='}},
}

-- Where every key is, and where the number is, for this view. Built again only when the room changes:
-- the numbers that come out of here are the numbers that paint and the numbers that take the finger.
local function build_layout()
  local margin = 16
  local gap = 10
  local cols, rows = 4, #Rows

  disp.x, disp.y, disp.w = margin, margin, W - 2 * margin
  disp.h = math.floor(H * 0.22)
  if disp.h > 200 then disp.h = 200 end
  if disp.h < 110 then disp.h = 110 end

  local pad_top = disp.y + disp.h + gap
  local cell_w = math.floor((disp.w - (cols - 1) * gap) / cols)
  local cell_h = math.floor((H - pad_top - margin - (rows - 1) * gap) / rows)

  -- **A FINGER'S HEIGHT IS NOT NEGOTIABLE** (63 px, the row of a list, docs/90). In a short view the
  -- display gives way so the keys keep it, because a key too small to press is a key that is not there.
  if cell_h < 63 then
    disp.h = disp.h - (63 - cell_h) * rows
    if disp.h < 80 then disp.h = 80 end

    pad_top = disp.y + disp.h + gap
    cell_h = math.floor((H - pad_top - margin - (rows - 1) * gap) / rows)
  end

  keys = {}

  for r = 1, rows do
    local col = 1
    local row = Rows[r]

    for c = 1, #row do
      local key = row[c]
      local span = key[3] or 1

      keys[#keys + 1] = {
        x = disp.x + (col - 1) * (cell_w + gap),
        y = pad_top + (r - 1) * (cell_h + gap),
        w = cell_w * span + gap * (span - 1),
        h = cell_h,
        label = key[1],
        action = key[2],
      }

      col = col + span
    end
  end
end

-- ----------------------------------------------------------- the arithmetic

-- **The display shows the sign and not the letter of the action**, which is the `Symbols` table of the
-- original (`["*"] = "×"`, `["/"] = "÷"`): an operation is read as a sign on the paper calculator.
local Symbols = {['+'] = '+', ['-'] = '-', ['*'] = kTimes, ['/'] = kDivide}

-- A number, the way the glass shows it.
local function fmt_number(n)
  if n ~= n or n == math.huge or n == -math.huge then return 'Error' end

  -- A whole number is shown as one, and `-0` is not a number anybody wants to see. `%.10g` already
  -- drops the trailing zeros of a fraction.
  if n == math.floor(n) and math.abs(n) < 1e15 then
    local s = string.format('%d', n)
    return (s == '-0') and '0' or s
  end

  return string.format('%.10g', n)
end

-- The one rule that matters here: `nil` means "this has no answer".
local function apply(a, op, b)
  if op == '+' then return a + b end
  if op == '-' then return a - b end
  if op == '*' then return a * b end

  if op == '/' then
    if b == 0 then return nil end
    return a / b
  end

  return b
end

-- What the big number says right now.
local function shown()
  if failed then return 'Error' end
  if entry ~= '' then return entry end
  if acc ~= nil then return fmt_number(acc) end
  return '0'
end

-- The small line over it: the operation that is waiting.
local function waiting()
  if pending == nil then return '' end
  return fmt_number(acc or 0) .. ' ' .. Symbols[pending]
end

local function reset()
  acc, pending, entry, fresh, failed = nil, nil, '', true, false
end

-- ---------------------------------------------------------- what a key does

local function type_digit(d)
  if fresh or entry == '' then
    entry, fresh = d, false
  elseif #entry < MaxDigits then
    entry = (entry == '0') and d or (entry .. d)
  end
end

local function type_dot()
  if fresh or entry == '' then
    entry, fresh = '0.', false
  elseif not entry:find('.', 1, true) and #entry < MaxDigits then
    entry = entry .. '.'
  end
end

local function backspace()
  if fresh then return end

  entry = entry:sub(1, -2)
  if entry == '' then fresh = true end
end

local function negate()
  if entry ~= '' then
    if entry:sub(1, 1) == '-' then
      entry = entry:sub(2)
    else
      entry = '-' .. entry
    end
  elseif acc ~= nil then
    acc = -acc
  end
end

local function choose(op)
  local cur = tonumber(entry)
  if cur == nil then cur = acc or 0 end

  -- With an operation waiting AND a second number typed, this key is what closes the one that was open:
  -- `2 + 3 -` leaves five on the way.
  if pending ~= nil and entry ~= '' then
    local r = apply(acc or 0, pending, cur)

    if r == nil then
      failed = true
      acc, pending, entry = nil, nil, ''
      return
    end

    acc = r
  elseif acc == nil then
    acc = cur
  end

  pending = op
  entry = ''
  fresh = true
end

local function equals()
  local cur = tonumber(entry)
  if cur == nil then cur = acc or 0 end

  -- **What was open when the key was pressed**: an `=` with no operation waiting only says the number that was already on
  -- the screen, which is not an operation and does not go to the history (docs/30).
  local operation = pending

  local expr

  if pending ~= nil then
    expr = fmt_number(acc or 0) .. ' ' .. Symbols[pending] .. ' ' .. fmt_number(cur)

    local r = apply(acc or 0, pending, cur)

    if r == nil then
      failed = true
      acc, pending, entry = nil, nil, ''
    else
      acc, pending, entry = r, nil, ''
    end
  else
    expr = fmt_number(cur)
    acc, entry = cur, ''
  end

  fresh = true

  -- **One line for the log and for the history**: the words are built once, so what the console says and what the page will
  -- show cannot drift apart (docs/30, docs/45).
  local line = expr .. ' = ' .. shown()
  if operation ~= nil then
    history[#history + 1] = line
    while #history > kMostHistory do table.remove(history, 1) end
  end

  console.print('Calculator: ' .. line)
end

local function is_operator(action)
  return action == '+' or action == '-' or action == '*' or action == '/'
end

local function press(key)
  -- **AN ERROR CLEARS ON THE NEXT KEY**, which is what every calculator does: the person has read it and
  -- wants to go on, not to press Clear first.
  if failed then reset() end

  if key.action == 'd' then
    type_digit(key.label)
  elseif key.action == 'dot' then
    type_dot()
  elseif key.action == 'clear' then
    reset()
  elseif key.action == 'back' then
    backspace()
  elseif key.action == 'neg' then
    negate()
  elseif key.action == '=' then
    equals()
  elseif is_operator(key.action) then
    choose(key.action)
  end

  -- **Every key leaves the view as it is** (the rule of docs/45 §5 bis): the block lives in the memory and is parked on the
  -- disk when the app ends, so writing it does not touch the flash at every digit and what is kept is what the glass shows
  -- (docs/30, docs/70).
  save_state()
end

-- The key a finger has landed on, or nothing. The rectangles are the painted ones: a key is where it is
-- seen.
local function key_at(x, y)
  for i = 1, #keys do
    local k = keys[i]

    if x >= k.x and x < k.x + k.w and y >= k.y and y < k.y + k.h then
      return k
    end
  end

  return nil
end

-- ------------------------------------------------------------------- the page

-- **The three keys that are not a figure have a grey of their own** (0.158.0, asked for by the owner): `C`
-- clears, the arrow eats the last digit and `=` closes the sum, and the four operations keep the light grey
-- the paper project gave them. The two numbers are in one place each so the page is changed here and not in
-- the loop: `12` is the original's (`draw.rect(k.x, k.y, k.w, k.h, 12)` of `pos_launch.lua`) and `8` is the
-- darker level the three actions wear, which reads at a glance where the eye has to press (docs/50, docs/90).
-- And a key of the dark level **wears white letters**: black on grey 8 is a label somebody has to look for.
local kOperatorGrey, kActionGrey = 12, 8

--- The grey a key is filled with, or nil when the key is paper and only has its frame.
local function key_grey(action)
  if action == 'clear' or action == 'back' or action == '=' then return kActionGrey end
  if is_operator(action) then return kOperatorGrey end

  return nil
end

-- **The display box and nothing else**: the waiting operation above and the number below, big and to the
-- right. It is what a key press paints, and it is white inside its frame before it does.
local function draw_display()
  ui.rect(disp.x + 1, disp.y + 1, disp.w - 2, disp.h - 2, 15)

  local hint = waiting()
  if hint ~= '' then
    ui.text(disp.x + 12, disp.y + 10, hint, kSmall, 8)
  end

  local text = shown()
  local step = 1

  -- **A number that runs out of its box is a number nobody reads**: the size steps down the ladder of the
  -- original —big, title, body, small— until it fits.
  while step < #Ladder and ui.text_width(text, Ladder[step]) > disp.w - 24 do
    step = step + 1
  end

  local scale = Ladder[step]

  -- **The height of the line is asked and not guessed** (0.121.0): the face of outlines of the disk is
  -- eleven pixels of line per step and the five by seven of the flash is seven, so the box would be off
  -- centre on one of the two boards with a number written here.
  local w = ui.text_width(text, scale)
  local h = ui.line_height(scale)
  ui.text(math.floor(disp.x + disp.w - 12 - w), math.floor(disp.y + disp.h - 12 - h), text, scale, 0)
end

local function draw_page()
  ui.clear(15)
  ui.frame(disp.x, disp.y, disp.w, disp.h)
  draw_display()

  -- The keypad. A key that is not a figure carries a grey of its own so the shape of the calculator is read
  -- before the labels are: the four operations the light one of the original and `C`, the arrow and `=` the
  -- dark one (`key_grey`).
  for i = 1, #keys do
    local k = keys[i]
    local grey = key_grey(k.action)

    if grey ~= nil then
      ui.rect(k.x, k.y, k.w, k.h, grey)
    end

    ui.frame(k.x, k.y, k.w, k.h)

    local lw = ui.text_width(k.label, kTitle)
    local lh = ui.line_height(kTitle)

    -- **The label is white on a dark key and black everywhere else** (0.158.0): the three keys of
    -- `kActionGrey` are the ones the eye has to find on the page, and a black letter over grey 8 is a
    -- letter that has to be looked for (docs/50, docs/90).
    local ink = (grey == kActionGrey) and 15 or 0

    ui.text(math.floor(k.x + (k.w - lw) / 2), math.floor(k.y + (k.h - lh) / 2), k.label, kTitle, ink)
  end
end

-- --------------------------------------------------------------------- the app

W, H = ui.caps().width, ui.caps().height
build_layout()

-- **The shape is read before anything is painted** (the rule of docs/30 § la sessió `state`): the number, the operation
-- that was waiting and the history are what the first `draw_page` below has to show (0.189.165).
restore()

-- **Where the keys are, said out loud when the app is asked to** (docs/30, docs/85): `run calculator keys`
-- prints the display box and the centre of every key, so a test presses a key without knowing the layout
-- and a change of layout cannot move the test's finger onto another key without the numbers saying so.
if app.args ~= nil and app.args.path == 'keys' then
  console.print(string.format('Calculator display %d %d %d %d', disp.x, disp.y, disp.w, disp.h))

  -- **The four sizes, in pixels of line**, which is the whole of what "the same look" means: the paper
  -- project's 33, 43, 57 and 82 and what this face really gives with its steps. A test reads this line so
  -- that the page cannot come out smaller without a number saying so (docs/30, docs/92).
  console.print(string.format('Calculator sizes %d %d %d %d', kSmall * ui.line_height(1),
    kBody * ui.line_height(1), kTitle * ui.line_height(1), kBig * ui.line_height(1)))

  -- **The four signs, measured on the face that is loaded** (0.158.0): a width of zero is a byte with no
  -- picture on this face, so a key that would paint a blank square says so in a number here instead of
  -- waiting for somebody to look at the glass (docs/30).
  console.print(string.format('Calculator symbols %d %d %d %d', ui.text_width(kDivide, kTitle),
    ui.text_width(kTimes, kTitle), ui.text_width(kPlusMinus, kTitle), ui.text_width(kArrow, kTitle)))

  for i = 1, #keys do
    local k = keys[i]
    console.print(string.format('Calculator key %s %d %d', k.label, math.floor(k.x + k.w / 2),
      math.floor(k.y + k.h / 2)))
  end
end

draw_page()
-- The whole page at once and with one quality: it is the first paint of an app and the glass has to be
-- clean (docs/50).
ui.present(kPageMode)

-- The finger is a contact and not a gesture: a key is pressed on the first frame that sees it, and held
-- means one press (`draw` reads the same frame, for the same reason).
local held = false

-- **The goodbyes of the page save too, and they go before the loop**: a key has already saved what it changed, and these
-- three cover what does not come from a key —the system closing the app, a sleep, a page going away—. They are **before**
-- the `while` perquè un tros que no s'acaba mai no registra res del que vingui després, que és el parany que es menja un
-- comiat escrit al final del fitxer (docs/30, docs/45).
function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

while true do
  -- The room of the view is what the layout is measured from: a turn of the page or a keyboard that came
  -- up gives another one, and the page is painted again from the new numbers.
  local view = ui.caps()
  if view.width ~= W or view.height ~= H then
    W, H = view.width, view.height
    build_layout()
    draw_page()
    ui.present(kPageMode)
  end

  local fingers = touch.read()
  if #fingers > 0 then
    if not held then
      held = true
      local key = key_at(fingers[1].x, fingers[1].y)
      if key ~= nil then
        press(key)
        -- **Only the number travels to the panel**: the keypad has not changed, so the damage is the
        -- display box and the wash is the one of a page of text.
        draw_display()
        ui.present(kKeyMode)
      end
    end
  elseif held then
    held = false
  end

  -- **The gestures of the system are taken and dropped**: the tap, the double and the triple of this
  -- frame are a calculator's keys already handled above, and a queue nobody empties is a queue that
  -- grows (docs/30).
  local gesture = touch.gesture()
  while gesture do
    gesture = touch.gesture()
  end

  sys.delay(20):await()
end

