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

-- The size of the glyphs, in steps of the face of the system: the number goes down this ladder until it
-- fits its box, and the keys are the third one —what the original called `title`— (docs/30).
local kBigScale = 4
local kKeyScale = 3

-- What the machine is doing: the left number, the operation waiting for its second operand, the digits
-- being typed, and whether the next digit opens a new number (after an operation, or after equals).
local acc = nil
local pending = nil
local entry = ''      -- exactly what the finger has typed, as it is shown
local fresh = true
local failed = false  -- dividing by zero: the next key clears the way

-- The view, and the two boxes measured from it.
local W, H = 0, 0
local disp = {x = 0, y = 0, w = 0, h = 0}
local keys = {}       -- one table a key: {x, y, w, h, label, action}

-- ------------------------------------------------------------------- the pad

-- The labels, the operation and the width. `span` is how many columns a key covers: the zero is double,
-- as it is on every calculator, and that is what leaves room for the point and equals in the same row.
local Rows = {
  {{'C', 'clear'}, {'<', 'back'}, {'+/-', 'neg'}, {'/', '/'}},
  {{'7', 'd'},     {'8', 'd'},    {'9', 'd'},     {'*', '*'}},
  {{'4', 'd'},     {'5', 'd'},    {'6', 'd'},     {'-', '-'}},
  {{'1', 'd'},     {'2', 'd'},    {'3', 'd'},     {'+', '+'}},
  {{'0', 'd', 2},  {'.', 'dot'},  {'=', '='}},
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

local Symbols = {['+'] = '+', ['-'] = '-', ['*'] = 'x', ['/'] = '/'}

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

  console.print('Calculator: ' .. expr .. ' = ' .. shown())
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

-- **The display box and nothing else**: the waiting operation above and the number below, big and to the
-- right. It is what a key press paints, and it is white inside its frame before it does.
local function draw_display()
  ui.rect(disp.x + 1, disp.y + 1, disp.w - 2, disp.h - 2, 15)

  local hint = waiting()
  if hint ~= '' then
    ui.text(disp.x + 12, disp.y + 10, hint, 1, 8)
  end

  local text = shown()
  local scale = kBigScale

  -- **A number that runs out of its box is a number nobody reads**: the size steps down until it fits.
  while scale > 1 and ui.text_width(text, scale) > disp.w - 24 do
    scale = scale - 1
  end

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

  -- The keypad. The operators and equals carry a grey of their own so the shape of the calculator is read
  -- before the labels are.
  for i = 1, #keys do
    local k = keys[i]

    if is_operator(k.action) or k.action == '=' then
      ui.rect(k.x, k.y, k.w, k.h, 12)
    end

    ui.frame(k.x, k.y, k.w, k.h)

    local lw = ui.text_width(k.label, kKeyScale)
    local lh = ui.line_height(kKeyScale)

    ui.text(math.floor(k.x + (k.w - lw) / 2), math.floor(k.y + (k.h - lh) / 2), k.label, kKeyScale, 0)
  end
end

-- --------------------------------------------------------------------- the app

ui.title('Calculator')

W, H = ui.caps().width, ui.caps().height
build_layout()

-- **Where the keys are, said out loud when the app is asked to** (docs/30, docs/85): `run calculator keys`
-- prints the display box and the centre of every key, so a test presses a key without knowing the layout
-- and a change of layout cannot move the test's finger onto another key without the numbers saying so.
if app.args ~= nil and app.args.path == 'keys' then
  console.print(string.format('Calculator display %d %d %d %d', disp.x, disp.y, disp.w, disp.h))

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
