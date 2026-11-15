-- settings: **el panell d'ajustos del sistema** (0.189.106, l'amo: *«Lua app de Settings de sistema … Farem un panell de
-- configurció clàssic, amb pestanyes agrupant controls per temática, butons, textbox, togglebox, desplegables…»*).
--
--   LES PESTANYES  clock, CPU, disk, input, system, battery i privacy, en una tira del sistema de tres files —les pestanyes
--                  del nucli saben fer-ne més d'una des d'aquesta versió (0.189.106)—.
--   LES FILES      **tots els controls són del sistema**: un `ui.checkbox` amb la seva paraula a dins, un `ui.selector` que
--                  obre un `ui.picker` del sistema, un `ui.button` per a les accions, un `ui.row` per al que només es
--                  llegeix i una `ui.dialog` per a les preguntes. No hi ha cap dibuix fet a mà ni cap llista pintada per
--                  l'app. **El que una fila ensenya es torna a llegir de la porta** cada cop que es col·loca, o sigui que
--                  s'hi veu el que el sistema ha acceptat i no el que l'app ha demanat (docs/45).
--   LA MIDA        **una vista nova dona a cada canvas de la pàgina el que tenia** (el pany de la pantalla en fa una), o
--                  sigui que cada escriptura passa per `settle()`: si `ui.caps()` té la mateixa mida, col·loca i pinta; si
--                  no, torna a construir-ho tot i deixa el cercle al control on era (docs/45).
--   QUI ESCRIU     **les portes del sistema**, que són les de la comanda `cfg` (0.189.106): `cfg.keyboard`, `cfg.boot`,
--                  `cfg.home`, `cfg.sort`, `cfg.turn`, `cfg.cursor`, més `time.tz`/`dst`/`leap`/`week`/`set`, `fs.cfg`,
--                  `power.cpu` i `admin.LockScreen`. Cada porta **respon el que queda dempeus**, i la pàgina torna a llegir
--                  el valor després d'escriure'l: el que s'ensenya és el que el sistema ha acceptat (docs/30, docs/70).
--
-- **El que aquesta app encara no fa, i per què** (docs/45): la llista blanca de freqüències de la CPU, la política de
-- miniatures, les sessions al disc, el NTP a l'arrencada, el paper pintat de fons, el splash propi, el PIN i els temps de
-- son demanen un subsistema que el nucli no té; les files no hi són per no ensenyar una porta que no obre res.
--
-- **I el que és de l'app i no del sistema**: la pestanya on era i el control amb el cercle, al bloc `state` de sempre.

local caps = sys.caps()
if not caps.display then
  console.print('settings: this board has no glass')
  return
end

local kLayout = 'v1'   -- puja quan un control canvia de nom o una pestanya canvia de forma
local kCheapMode = 'du'

local TABS = { 'Clock', 'CPU', 'Disk', 'Input', 'System', 'Battery', 'Privacy' }
local MARGIN, GAP, ROW, NOTE_H, TABS_H = 12, 8, 56, 56, 156

local page = 1
local hint = ''

local wid, hei = 0, 0

-- Les paraules dels desplegables, que són de l'app i no del control.
local HOURS, MINUTES, DAYS = {}, {}, {}
for i = 0, 23 do HOURS[i + 1] = string.format('%02d', i) end
for i = 0, 59 do MINUTES[i + 1] = string.format('%02d', i) end
for i = 1, 31 do DAYS[i] = string.format('%d', i) end
local MONTHS = { 'January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
                 'November', 'December' }

-- **Els desfasaments de mitja hora** de -12:00 a +14:00, que és tot el que un rellotge pot dir: the list is what the picker
-- offers and `time.tz` takes minutes, so the two agree by construction (docs/30, docs/60).
local OFFSETS = {}
for i = 1, 2 * (14 + 12) + 1 do
  local minutes = (i - 1) * 30 - 12 * 60
  local sign = (minutes < 0) and '-' or '+'
  local m = math.abs(minutes)
  OFFSETS[i] = string.format('%s%02d:%02d', sign, m // 60, m % 60)
end

local SPEEDS, SPEED_MHZ = { '80 MHz', '160 MHz', '240 MHz' }, { 80, 160, 240 }
local TURNS, TURN_VALUE = { '5 000', '10 000', '20 000', '40 000', '80 000' }, { 5000, 10000, 20000, 40000, 80000 }
local CURSORS, CURSOR_MS = { '600 ms', '850 ms', '1 100 ms' }, { 600, 850, 1100 }
local UNITS = { 'auto', 'sd', 'littlefs' }
local WEEKS = { 'Monday', 'Sunday' }

-- ---------------------------------------------------------------- la sessió `state`
--
-- **La pestanya on era i el control amb el cercle, i res més** (docs/30): the panel's settings are the system's and live in the
-- store of the kernel; what belongs to the page is where the person was.
local shape = { focus = nil }
local tabs, note
local rows = {}          -- per pestanya, les files en ordre
local named = {}         -- el nom de cada control, per la sessió
local words_of = {}      -- les paraules d'una casella, que van després de la càpsula
local refresh_of = {}    -- com es torna a llegir el que ensenya una fila

local session = nil

local function the_state()
  if session == nil then
    session = sys.session('state', 1024)
  end
  return session
end

local function save_state()
  local block = the_state()
  if block == nil then return end
  local lines = { kLayout, 'tab=' .. page }
  if shape.focus ~= nil then lines[#lines + 1] = 'focus=' .. shape.focus end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, 1024) or ''
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end
  local was = {}
  local first = true
  for line in body:gmatch('[^\n]+') do
    if first then
      first = false
      if line ~= kLayout then return nil end
    else
      local key, value = line:match('^([%w_]+)=(.*)$')
      if key ~= nil then was[key] = value end
    end
  end
  if first then return nil end
  return was
end

-- ---------------------------------------------------------------- les targetes del sistema
--
-- **El que un control demana es fa al torn següent i no dins del seu ganxo**: the system repaints the button that was released
-- after the hook has repainted the page, and a page painted inside the gesture keeps the old word where it no longer is
-- (docs/30, docs/90).
local wanted = nil
local card = nil
local card_field = nil
-- **La pàgina es torna a col·locar i a pintar des de dins d'una targeta**, i les targetes es fan abans que les dues funcions
-- que ho saben fer: sense la declaració de davant, `layout` seria un global i el `wanted` petaria en el primer toc (docs/45).
local layout, paint_page, settle, build

local function say(what)
  hint = what or ''
  if note ~= nil then note.text = hint end
end

-- **La targeta no arriba mai al fons de la pàgina**: a card of the system owns the fingers that fall on it, so the one way
-- out of a picker that changes its mind is a strip of paper beside it, and a card that ends at the bottom edge leaves no
-- paper to tap (docs/30, docs/90).
local kTapStrip = 40

local function card_top() return MARGIN + TABS_H + GAP end

local function card_height()
  return math.max(240, hei - MARGIN - NOTE_H - kTapStrip - card_top())
end

local function close_card()
  if card ~= nil then
    card:release()
    card = nil
  end
  if card_field ~= nil then
    card_field.choosing = false
    card_field = nil
  end
end

local function open_picker(field, put_index)
  close_card()
  if type(field.items) ~= 'table' or #field.items == 0 then
    console.print('settings: the field ' .. tostring(field.label) .. ' has no values on this board')
    say('That setting offers no values on this board')
    return
  end
  local picker, why = ui.picker(wid - 2 * MARGIN, card_height())
  if picker == nil then
    console.print('settings: no card (' .. tostring(why and why.code or 'the memory') .. ')')
    return
  end
  picker.title = field.label
  picker.items = field.items
  picker.index = field.index
  picker.on_pick = function(self, index)
    wanted = function()
      close_card()
      if index ~= nil and index > 0 then put_index(index) end
      settle()
    end
  end
  picker:place(MARGIN, card_top())
  card, card_field = picker, field
  field.choosing = true
  ui.focus(picker)
  ui.present(kCheapMode)
end

-- **La pregunta d'una esborrada és una targeta del sistema** (docs/30): `ui.dialog` amb el seu títol, el seu text i els dos
-- botons; l'app només diu què vol preguntar i què fer si la resposta és sí.
local function ask(title, text, on_yes, on_no)
  close_card()
  local dialog, why = ui.dialog(wid - 2 * MARGIN, card_height())
  if dialog == nil then
    console.print('settings: no card for the question (' .. tostring(why and why.code or 'the memory') .. ')')
    return
  end
  dialog.title = title
  dialog.text = text
  dialog.accept = 'Erase'
  dialog.cancel = 'Keep'
  dialog.on_accept = function()
    wanted = function()
      close_card()
      if on_yes ~= nil then on_yes() end
      settle()
    end
  end
  dialog.on_cancel = function()
    wanted = function()
      close_card()
      if on_no ~= nil then on_no() end
      settle()
    end
  end
  dialog:place(MARGIN, card_top())
  card = dialog
  card_field = nil
  ui.focus(dialog)
  ui.present(kCheapMode)
end

-- ---------------------------------------------------------------- les files

local function remember(on, name)
  if on then shape.focus = name end
end

-- **Una fila de casella**: el control del sistema amb la paraula a dins, que és com el `life` el fa servir (docs/45).
local function check_row(tab, name, label, get, put)
  local box, why = ui.checkbox(2, 2)
  if box == nil then
    console.print('settings: no box for ' .. name .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
    return nil
  end
  local words, why2 = ui.label(2, 2)
  if words == nil then
    box:release()
    console.print('settings: no words for ' .. name .. ' (' .. tostring(why2 and why2.code or 'the memory') .. ')')
    return nil
  end
  words.text = label
  words.valign = 'middle'
  box:add(words)
  words_of[box] = words
  box.checked = get() and true or false
  refresh_of[box] = function() box.checked = get() and true or false end
  box.on_focus = function(self, on) remember(on, name) end
  box.on_change = function(self, on)
    wanted = function()
      put(on)
      settle()
    end
  end
  named[name] = box
  rows[tab][#rows[tab] + 1] = box
  return box
end

-- **Una fila que s'escull**: el camp del sistema i la targeta que obre, amb la traducció entre el número de la llista i el
-- valor del sistema. **El valor es torna a llegir de la porta** després d'escriure'l, o sigui que un ajust que el sistema
-- refusa es veu tornar al que hi havia (docs/30).
local function choice_row(tab, name, label, items, get_index, put_index)
  local one, why = ui.selector(2, 2)
  if one == nil then
    console.print('settings: no field for ' .. name .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
    return nil
  end
  one.label = label
  one.items = items
  one.empty = '-'
  one.index = get_index()
  refresh_of[one] = function() one.index = get_index() end
  one.on_open = function(self)
    wanted = function() open_picker(self, put_index) end
  end
  one.on_focus = function(self, on) remember(on, name) end
  named[name] = one
  rows[tab][#rows[tab] + 1] = one
  return one
end

local function button_row(tab, name, label, on_click)
  local one, why = ui.button(2, 2)
  if one == nil then
    console.print('settings: no button for ' .. name .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
    return nil
  end
  one.title = label
  one.on_click = function() wanted = function() on_click() end end
  one.on_focus = function(self, on) remember(on, name) end
  named[name] = one
  rows[tab][#rows[tab] + 1] = one
  return one
end

-- **Una fila que només es llegeix**: el `ui.row` del sistema, amb el nom a l'esquerra i el número a la dreta. No és
-- enfocable i no s'escriu: el que diu és el que la placa ha respost quan la pestanya s'ha col·locat (docs/30).
local function read_row(tab, name, label, get_text)
  local one, why = ui.row(2, 2)
  if one == nil then
    console.print('settings: no row for ' .. name .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
    return nil
  end
  one.title = label
  refresh_of[one] = function() one.detail = get_text() end
  named[name] = one
  rows[tab][#rows[tab] + 1] = one
  return one
end

local function index_of(list, value)
  for i, one in ipairs(list) do
    if one == value then return i end
  end
  return 0
end

-- ---------------------------------------------------------------- l'hora i la data
--
-- **L'època d'un moment local** (docs/60): the RTC counts UTC and a person reads their own hour, so the epoch of "today at
-- 09:30" is the days since 1970 times a day plus the hour, less the offset of the map. The arithmetic of a civil day is the
-- one every calendar of this system ends up needing.
local function days_from_civil(y, m, d)
  if m <= 2 then y = y - 1 end
  local era = (y >= 0) and y or (y - 399)
  era = era // 400
  local yoe = y - era * 400
  local mp = (m + 9) % 12
  local doy = (153 * mp + 2) // 5 + d - 1
  local doe = yoe * 365 + yoe // 4 - yoe // 100 + doy
  return era * 146097 + doe - 719468
end

local function epoch_of(day, month, hour, minute)
  local today = time.date()
  if today == nil then return nil end
  local offset = time.tz() or 0
  return days_from_civil(today.year, month, day) * 86400 + hour * 3600 + minute * 60 - offset * 60
end

local function tz_here()
  local minutes = time.tz() or 0
  local sign = (minutes < 0) and '-' or '+'
  local m = math.abs(minutes)
  return string.format('%s%02d:%02d', sign, m // 60, m % 60)
end

-- ---------------------------------------------------------------- les pestanyes i les files

-- **Les paraules d'una casella van després de la càpsula, i la càpsula només se sap quan la fila té amplada**: el fill
-- d'un `ui.checkbox` és un label i el sistema en diu la caixa amb `cb:box()`, que és la mesura de la imatge i no un número
-- de l'app (docs/30, docs/45).
local function place(control, y, h)
  local width = wid - 2 * MARGIN
  control:resize(width, h)
  control:place(MARGIN, y)
  local words = words_of[control]
  if words ~= nil then
    local left = control:box() + 10
    words:resize(math.max(0, width - left - 8), h)
    words:place(left, 0)
  end
  -- **El que una fila ensenya es torna a llegir del sistema cada cop que es col·loca**: the field of a choice keeps the
  -- `index` the app gave it, so a write that happened in a card would leave the page saying the old value (docs/30).
  local refresh = refresh_of[control]
  if refresh ~= nil then refresh() end
end

function layout()
  tabs:resize(wid - 2 * MARGIN, TABS_H)
  tabs:place(MARGIN, MARGIN)
  for i = 1, #TABS do
    for _, control in ipairs(rows[i] or {}) do
      control:place(0, hei + 400)
    end
  end
  local y = MARGIN + TABS_H + GAP
  for _, control in ipairs(rows[page] or {}) do
    place(control, y, ROW)
    y = y + ROW + GAP
  end
  if note ~= nil then
    note:resize(wid - 2 * MARGIN, NOTE_H)
    note:place(MARGIN, hei - MARGIN - NOTE_H)
    note.text = hint
  end
end

function paint_page()
  ui.clear(15, kCheapMode)
  ui.present(kCheapMode)
end

-- **Una escriptura pot tornar a fer el vidre** (0.189.106): the lock of the screen takes the bar away and the system makes the
-- view again, and a view made again gives every canvas of the page back —the control the finger pressed included—, so the
-- page is built again in the box it has now. Every road that writes asks this instead of laying the page out, and a page
-- whose box has not changed is the page it already had: one comparison and one present, which is what a write always cost
-- (docs/30, docs/45, docs/90).
function settle()
  local c = ui.caps()
  if c.width == wid and c.height == hei then
    layout()
    paint_page()
    return
  end
  wid, hei = c.width, c.height
  close_card()
  words_of, refresh_of, named = {}, {}, {}
  if not build() then return end
  layout()
  paint_page()
  if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end
end

function build()
  local c = ui.caps()
  wid, hei = c.width, c.height
  for i = 1, #TABS do rows[i] = {} end

  local why
  tabs, why = ui.tabs(2, 2)
  if tabs == nil then
    console.print('settings: no strip of tabs (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  tabs.items = TABS
  tabs.index = page
  tabs.scale = 2
  tabs.on_change = function(self, index)
    if index == nil or index == page then return end
    wanted = function()
      close_card()
      page = index
      say('')
      settle()
      save_state()
    end
  end
  named.tabs = tabs

  note, why = ui.label(2, 2)
  if note == nil then
    console.print('settings: no line of help (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  note.text = ''
  note.scale = 2

  -- ---- la pestanya del rellotge: l'hora, la data, i els tres ajustos del calendari
  local today = time.date()
  local clock = {
    hour = (today ~= nil) and today.hour or 0,
    minute = (today ~= nil) and today.min or 0,
    day = (today ~= nil) and today.day or 1,
    month = (today ~= nil) and today.month or 1,
  }
  choice_row(1, 'hour', 'Hour', HOURS, function() return clock.hour + 1 end, function(index)
    clock.hour = index - 1
    say('')
  end)
  choice_row(1, 'minute', 'Minute', MINUTES, function() return clock.minute + 1 end, function(index)
    clock.minute = index - 1
    say('')
  end)
  choice_row(1, 'day', 'Day', DAYS, function() return clock.day end, function(index) clock.day = index end)
  choice_row(1, 'month', 'Month', MONTHS, function() return clock.month end, function(index) clock.month = index end)
  button_row(1, 'setclock', 'Set the clock', function()
    local epoch = epoch_of(clock.day, clock.month, clock.hour, clock.minute)
    if epoch == nil then
      say('This board has no calendar to write an hour into')
      return
    end
    local ok, kept = pcall(time.set, epoch)
    if ok and type(kept) == 'number' then
      say(string.format('The clock is set to %02d:%02d, %d %s', clock.hour, clock.minute, clock.day,
                        MONTHS[clock.month]))
      console.print(string.format('settings: the clock is set to %02d:%02d on day %d of month %d', clock.hour,
                                  clock.minute, clock.day, clock.month))
    else
      say('This board does not take an hour')
      console.print('settings: the clock refuses the hour (' .. tostring(ok and kept or kept.code) .. ')')
    end
  end)
  check_row(1, 'dst', 'Daylight saving time', function() return time.dst() end, function(on) time.dst(on) end)
  choice_row(1, 'tz', 'Time zone', OFFSETS, function() return index_of(OFFSETS, tz_here()) end, function(index)
    local text = OFFSETS[index]
    local sign = (text:sub(1, 1) == '-') and -1 or 1
    local h, m = text:match('^[+-](%d+):(%d+)$')
    if h ~= nil then time.tz(sign * (tonumber(h) * 60 + tonumber(m))) end
  end)
  choice_row(1, 'week', 'First day', WEEKS, function() return (time.week() == 7) and 2 or 1 end,
             function(index) time.week((index == 2) and 7 or 1) end)
  check_row(1, 'leap', 'Leap year', function() return time.leap() end, function(on) time.leap(on) end)
  button_row(1, 'ntp', 'Sync from the network', function()
    local ok, err = pcall(net.ntp)
    say(ok and 'The hour comes from the network' or ('No network: ' .. tostring(err and err.code or err)))
  end)

  -- ---- CPU
  choice_row(2, 'speed', 'Speed', SPEEDS, function() return index_of(SPEED_MHZ, power.cpu()) end,
             function(index) power.cpu(SPEED_MHZ[index]) end)
  choice_row(2, 'turn', 'Turn', TURNS, function() return index_of(TURN_VALUE, cfg.turn()) end,
             function(index) cfg.turn(TURN_VALUE[index]) end)

  -- ---- Disc
  local function format_volume(root, what)
    ask('Erase ' .. what, 'Every file of ' .. what .. ' goes away, and it cannot be taken back.',
        function()
          local ok, err = pcall(fs.format, root)
          if ok then
            say(what .. ' is empty')
            console.print('settings: ' .. what .. ' is empty')
          else
            say(what .. ' could not be erased')
            console.print('settings: ' .. what .. ' refused the format (' .. tostring(err and err.code or err) .. ')')
          end
        end)
  end
  choice_row(3, 'unit', 'Preferred disk', UNITS, function() return index_of(UNITS, (fs.cfg() or {}).unit or '') end,
             function(index) fs.cfg(UNITS[index]) end)
  button_row(3, 'format-nvs', 'Format the store', function() format_volume('/nvs', 'the store') end)
  button_row(3, 'format-flash', 'Format the flash', function() format_volume('/littlefs', 'the flash') end)
  button_row(3, 'format-sd', 'Format the card', function() format_volume('/sd', 'the card') end)

  -- ---- Input
  local keyboards = cfg.keyboards() or {}
  choice_row(4, 'keyboard', 'Keyboard', keyboards, function() return index_of(keyboards, cfg.keyboard() or '') end,
             function(index) cfg.keyboard(keyboards[index]) end)
  choice_row(4, 'cursor', 'Cursor blink', CURSORS, function() return index_of(CURSOR_MS, cfg.cursor()) end,
             function(index) cfg.cursor(CURSOR_MS[index]) end)
  check_row(4, 'sort', 'Order by uses', function() return cfg.sort() end, function(on) cfg.sort(on) end)
  do
    local boots = cfg.boots() or {}
    choice_row(4, 'boot', 'Boot picture', boots, function() return index_of(boots, cfg.boot() or '') end,
               function(index) cfg.boot(boots[index]) end)
  end

  -- ---- System
  local list = { 'none' }
  local ok, mine = pcall(apps.list)
  if ok and type(mine) == 'table' then
    for _, id in ipairs(mine) do list[#list + 1] = id end
  end
  choice_row(5, 'home', 'App at start', list, function() return index_of(list, cfg.home() or 'none') end,
             function(index) cfg.home(list[index]) end)

  -- ---- Battery: **el que la placa sap de la seva pila**, que és un número i no un ajust d'aquesta pàgina (docs/30). Les
  -- files són del sistema i es tornen a llegir cada cop que la pestanya es col·loca.
  local reading = 'the battery is not read yet'
  read_row(6, 'battery', 'Battery', function()
    local ok2, one = pcall(power.battery)
    if ok2 and type(one) == 'table' then
      reading = string.format('%d%% of %d mV%s', one.pct or 0, one.mv or 0, one.charging and ', charging' or '')
    else
      reading = 'this board does not know'
    end
    return reading
  end)
  read_row(6, 'board', 'Board', function() return string.format('%s, %d MHz', sys.board, power.cpu()) end)

  -- ---- Privacy
  local locked = (admin ~= nil and admin.LockScreen ~= nil) and (admin.LockScreen() and true or false) or false
  check_row(7, 'lock', 'Lock the screen', function() return locked end, function(on)
    if admin == nil or admin.LockScreen == nil then
      say('This app is not an administrator on this board')
      return
    end
    local ok3, kept = pcall(admin.LockScreen, on)
    locked = ok3 and (kept and true or false) or false
    say(locked and 'The bar is off until the screen is unlocked' or '')
  end)

  return true
end

-- ---------------------------------------------------------------- l'app

if not build() then
  console.print('settings: the page cannot be built')
  return
end

do
  local was = load_state()
  if was ~= nil then
    local tab = tonumber(was.tab or '')
    if tab ~= nil then page = math.min(math.max(1, tab), #TABS) end
    shape.focus = was.focus
    tabs.index = page
  end
end

layout()
paint_page()
if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end

console.print('settings: ' .. #TABS .. ' tabs, and the doors of cfg behind them')

function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

while true do
  if wanted ~= nil then
    local doing = wanted
    wanted = nil
    sys.delay(300):await()
    doing()
  end

  -- **Un canvi de mida torna a fer la pàgina sencera** (0.189.106): the system makes a new view for the whole glass or for
  -- the band of the keyboard, and a view made again gives every canvas of the page back —the controls the page had are not
  -- on the screen any more— so the page is built again with the box it has now, which is what the timeclock does (docs/30,
  -- docs/45, docs/90). The question costs one call per turn and the page is painted only when the answer is a new box.
  local c = ui.caps()
  if c.width ~= wid or c.height ~= hei then settle() end

  -- **Un toc fora de la targeta la tanca**, que és el que vol dir una targeta sobre una pàgina: the card of the system owns
  -- the fingers that fall on it and the page under it answers nothing, so the way out is a gesture beside it (docs/30, docs/90).
  local gesture = touch.gesture()
  while gesture do
    if card ~= nil and (gesture.kind == 'tap' or gesture.kind == 'long') then
      local where = card:caps()
      if gesture.x < where.x or gesture.x >= where.x + where.width or gesture.y < where.y or
         gesture.y >= where.y + where.height then
        wanted = function()
          close_card()
          paint_page()
        end
      end
    end
    gesture = touch.gesture()
  end

  sys.delay(150):await()
end
