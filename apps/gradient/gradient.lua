-- gradient: the sixteen levels of this glass, as a test card (0.190.86).
--
-- **Aquest és el full de proves dels grisos.** El panell d'aquest sistema té setze nivells i el que una persona veu és
-- quantes d'aquestes setze es poden comptar: el que aquesta pàgina fa és ensenyar-les **numerades**, amb el dibuix que
-- fa evident on es fonen (una barra sola no ho diu; setze barres amb el número a dins, sí).
--
-- **Quatre proves i no una**, perquè cada una diu una cosa diferent:
--
--   - `steps`: setze barres verticals, nivell 0 a 15 d'esquerra a dreta, amb el número dins de cada barra. Si dues barres
--     són el mateix color, el vidre no separa aquells dos nivells — i el número diu **quins** són.
--   - `ramp`: un degradat continu de 0 a 15 a l'amplada, i el mateix de dalt a baix a la dreta. L'horitzontal diu si el
--     vidre fa esglaons; el vertical, si la fila (que és com el panell escriu la tinta) introdueix alguna cosa.
--   - `blocks`: cinc taques grans —0, 4, 8, 12, 15—. Una taca gran de gris és el que ensenya si el to és el que toca; les
--     barres primes poden enganyar per la vora.
--   - `halves`: mig vidre negre i mig blanc, la prova que fa caure un panell que no arriba als extrems.
--
-- **I la tinta amb què es pinta és triable** (0.190.86): `gc16` és la que neteja (matriu de transició 16×16, la que ha de
-- fer setze grisos), `gl16` la que no neteja, i `du` la de dues tintes —que ha de **col·lapsar** el degradat en dues bandes,
-- i si no ho fa és que el mode no és el que diu—. El nom de la tinta surt a la barra del sistema, o sigui que una foto del
-- vidre diu sola quina de les tres s'hi veu.

local caps = ui.caps()
local view = { width = caps.width, height = caps.height }
if view.width <= 0 or view.height <= 0 then
  console.print('gradient: this board has no view')
  return
end

-- The state of this page, and the pair that writes it and reads it (docs/30 § «La sessió `state`»). What a person leaves
-- here is **which test and which ink**, que és la forma de la vista i no una preferència.
local kStateBytes = 1024
local kLayout = 'v1'

local state = nil
local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end
  return state
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'focus', 'page' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
  end
  if now.ink ~= nil then lines[#lines + 1] = 'ink=' .. tostring(now.ink) end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  -- The block is a kilobyte and what is written is shorter: the rest is the zero it was made with, and a line with zeros
  -- behind it is not a value (0.189.52).
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

-- The tests, in the order the button walks them, and the inks the panel can be asked for.
local kTests = { 'steps', 'ramp', 'blocks', 'halves' }
local kInks = { 'gc16', 'gl16', 'du' }

local now = load_state() or {}
local page = tonumber(now.page) or 1
page = math.min(math.max(1, page), #kTests)
local ink = 1
for index, name in ipairs(kInks) do
  if name == now.ink then ink = index end
end

-- One line of the body face, and the rows the page keeps for the title and the two buttons.
local kScale = 2
local kLine = ui.line_height(kScale)
if kLine <= 0 then kLine = kScale * 11 end
local kMargin = 12
local kButtonH = 56
local kButtonW = 150
local kFoot = kMargin + kButtonH
local kTop = kMargin + kLine + kMargin
local board = { x = 0, y = kTop, w = view.width, h = view.height - kTop - kFoot }
if board.h < 40 then board.h = 40 end

-- **El número dins d'una barra, amb la tinta contrària**: a label has a paper of its own, o sigui que la barra no ha de
-- decidir res —el text es pinta sobre el que hi hagi i el que hi ha és el nivell de la barra— (docs/30, docs/92).
local function number_in(x, y, w, h, level)
  local text = tostring(level)
  local tw = ui.text_width(text, 1)
  local tx = x + math.floor((w - tw) / 2 + 0.5)
  local ty = y + math.floor((h - ui.line_height(1)) / 2 + 0.5)
  if (level >= 8) then
    -- Paper clar: el número va en negre.
    ui.text(tx, ty, text, 1, 0)
  else
    ui.text(tx, ty, text, 1, 15)
  end
end

-- **Les setze barres, i el número de cadascuna dins** (0.190.86): `math.floor` a la frontera de la darrera, o les setze
-- barres de 33 píxels deixen tres columnes mortes a la dreta i la darrera es menja el que sobra.
local function draw_steps()
  local bar_w = math.floor(board.w / 16)
  for level = 0, 15 do
    local x = level * bar_w
    local w = (level == 15) and (board.w - x) or bar_w
    ui.rect(x, board.y, w, board.h, level)
    number_in(x, board.y + 8, w, 24, level)
  end
end

-- **El degradat horitzontal i el vertical** (0.190.86): every column is a level and every row of the strip on the right is
-- one too, o sigui que el que es veu a la vora dreta és el panell escrivint la tinta línia a línia.
local function draw_ramp()
  local strip = math.floor(board.w / 8)
  local wide = board.w - strip
  for x = 0, wide - 1 do
    local level = math.floor(x * 15 / (wide - 1) + 0.5)
    ui.rect(x, board.y, 1, board.h, level)
  end
  for y = 0, board.h - 1 do
    local level = math.floor(y * 15 / (board.h - 1) + 0.5)
    ui.rect(wide, board.y + y, strip, 1, level)
  end
  -- The sixteen ticks, so a ramp that came out as five bands can be read against the number it should have been.
  for level = 0, 15 do
    local y = board.y + math.floor(level * (board.h - 1) / 15 + 0.5)
    ui.rect(wide, y, strip, 1, 0)
  end
end

-- **Cinc taques grans** (0.190.86): the grey of a big patch is what a person calls «the tone»; a two-pixel bar is an edge.
local function draw_blocks()
  local levels = { 0, 4, 8, 12, 15 }
  local each = math.floor(board.w / #levels)
  for index, level in ipairs(levels) do
    local x = (index - 1) * each
    local w = (index == #levels) and (board.w - x) or each
    ui.rect(x, board.y, w, board.h, level)
    -- The number goes in the opposite ink, and the caption of a patch is a whole line of the body face.
    local tw = ui.text_width(tostring(level), kScale)
    local ink_level = (level >= 8) and 0 or 15
    ui.text(x + math.floor((w - tw) / 2 + 0.5), board.y + math.floor(board.h / 2 - kLine / 2), tostring(level), kScale,
            ink_level)
  end
end

-- **Mig vidre de cada** (0.190.86): the two rails of the panel, which is the picture that falls over when a driver does not
-- reach them — it is the test that needs no counting at all.
local function draw_halves()
  local half = math.floor(board.w / 2)
  ui.rect(0, board.y, half, board.h, 0)
  ui.rect(half, board.y, board.w - half, board.h, 15)
  local tw = ui.text_width('0', kScale)
  ui.text(math.floor(half / 2 - tw / 2), board.y + math.floor(board.h / 2 - kLine / 2), '0', kScale, 15)
  ui.text(half + math.floor((board.w - half) / 2 - tw / 2), board.y + math.floor(board.h / 2 - kLine / 2), '15', kScale,
          0)
end

local painters = {
  steps = draw_steps,
  ramp = draw_ramp,
  blocks = draw_blocks,
  halves = draw_halves,
}

-- The two keys of the page. **Botons i no un toc a qualsevol lloc**: this system's taps belong to the controls, and a page
-- that reacted to any finger would be a page where the system's own card could not be opened without changing the test.
local test_button = nil
local ink_button = nil

local function say_what_is_on_the_glass()
  -- The bar says the ink, because a photograph of the glass is then a measurement and not an opinion.
  ui.title('Gradient: ' .. kTests[page] .. ' / ' .. kInks[ink])
end

local function paint()
  ui.clear(15)
  say_what_is_on_the_glass()
  ui.text(kMargin, kMargin, kTests[page] .. '  ·  ' .. kInks[ink], kScale, 0)
  painters[kTests[page]]()
  if test_button ~= nil then test_button:place(kMargin, view.height - kButtonH) end
  if ink_button ~= nil then ink_button:place(view.width - kMargin - kButtonW, view.height - kButtonH) end
  -- **La tinta la tria aquesta pàgina i no el sistema** (docs/30): the whole point of the test is to paint the same picture
  -- in a named waveform, o sigui que el `present` porta el mode i no el deixa decidir.
  ui.present(kInks[ink])
  save_state({ page = page, ink = kInks[ink] })
end

local function next_test()
  page = page % #kTests + 1
  paint()
end

local function next_ink()
  ink = ink % #kInks + 1
  paint()
end

test_button = ui.button(kButtonW, kButtonH)
ink_button = ui.button(kButtonW, kButtonH)
if test_button ~= nil then
  test_button.title = 'Test'
  test_button.on_click = function() next_test() end
end
if ink_button ~= nil then
  ink_button.title = 'Ink'
  ink_button.on_click = function() next_ink() end
end

-- **El dial també camina les proves** (docs/30): a board with no finger on the glass —or a person holding the middle key—
-- has the same two questions, and the first key of the dial asks them.
function on_button(kind, held_ms, key)
  if held_ms ~= nil and held_ms > 800 then return end
  if kind == 'press' and key == 1 then next_test() end
end

function on_resize()
  local fresh = ui.caps()
  view.width = fresh.width
  view.height = fresh.height
  board.y = kTop
  board.h = view.height - kTop - kFoot
  if board.h < 40 then board.h = 40 end
  paint()
end

function on_pause() save_state({ page = page, ink = kInks[ink] }) end
function on_save() save_state({ page = page, ink = kInks[ink] }) end
function on_close() save_state({ page = page, ink = kInks[ink] }) end

paint()
while true do sys.delay(200):await() end
