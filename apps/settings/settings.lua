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
--   QUI ESCRIU     **les portes del sistema**, que són les de la comanda `cfg`: `cfg.keyboard`, `cfg.boot`, `cfg.home`,
--                  `cfg.sort`, `cfg.turn`, `cfg.cursor`, `cfg.freq` (les tres velocitats que el governador pot fer servir),
--                  `cfg.settings` (l'app que obre el botó dels ajustos del panell), `cfg.lock`/`cfg.lock_on`/`cfg.lock_after`
--                  (la pàgina que tapa el vidre, si el bloqueig és actiu i quants minuts sense màns el disparen; 0.189.150),
--                  més `time.tz`/`dst`/`leap`/`week`/`set`, `fs.cfg`, `power.cpu` i `power.cpu_auto`. Cada porta **respon el que
--                  queda dempeus**, i la pàgina torna a llegir el valor després d'escriure'l: el que s'ensenya és el que el
--                  sistema ha acceptat (docs/30, docs/70).
--   LA MESURA      la pestanya de la CPU **diu què és el número d'un torn i què val en aquesta placa**: la volta de
--                  `k = k + 1` en dona el preu d'una instrucció i `sys.stats()` els torns de debò que ha fet el sistema i el
--                  que hi ha de Lua a dins (0.189.108, docs/75 i docs/95).
--
-- **El que aquesta app encara no fa, i per què** (docs/45): la política de miniatures, les sessions al disc, el NTP a
-- l'arrencada, el paper pintat de fons, el splash propi i el PIN demanen un subsistema que el nucli no té; les files no hi són
-- per no ensenyar una porta que no obre res. **Els temps de son ja no són d'aquesta llista** (0.190.5): el nucli té la política
-- d'adormir-se i les dues files de la pestanya Battery —`Sleep when idle` i `Sleep after`— l'apaguen, la desen i li diuen quanta estona de
-- quietud vol (docs/60).
--
-- **I el que és de l'app i no del sistema**: la pestanya on era i el control amb el cercle, al bloc `state` de sempre.

local caps = sys.caps()
if not caps.display then
  console.print('settings: this board has no glass')
  return
end

-- **La forma que es desa no ha canviat** (0.189.148): the `state` of this page is the tab and the name of the control with
-- the ring, and the two rows that the CPU tab gained are `read_row`s —`ui.row` is born `focusable = false`, so a read row
-- never takes the dial and its name never reaches the block—. A `kLayout` that went up here would throw away the tab a
-- person left for nothing, which is the one cosa que el bloc existeix per a recordar (docs/30, docs/45 §5 bis).
-- **v2** (0.190.5): el commutador `sleep` va passar de la pestanya System a la de Battery, i un bloc que diu «pestanya 5 i cercle a
-- `sleep`» és una parella que ja no existeix; el que una persona havia deixat es torna a triar una vegada i prou (docs/30).
local kLayout = 'v2'   -- puja quan un control AMB FOCUS canvia de nom, o quan la forma del bloc canvia
local kCheapMode = 'du'
-- **Quant s'espera una hora d'un servidor** (0.189.143): the same deadline the page of the time zone asks with, because the two
-- buttons are the same request and a person should not wait longer on one page than on the other (docs/30, docs/60).
local kSyncTimeoutMs = 8000

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

-- **Les zones són hores senceres** (0.189.142, l'amo: *«Treu la selecció de +30 a les zones horàries, no he vist mai que fos
-- +X.5, sempre hores +1, -3, +3…»*): de -12:00 a +14:00, una hora per fila, i cap mitja hora a la llista. El sistema **sí** que
-- accepta qualsevol minut —`cfg tz 90` és una hora i mitja i `time.tz` no discuteix—, i una placa posada així ensenya la fila
-- buida (`empty = '-'`) en lloc d'un número que el desplegable no pot tornar a escriure: el que la pàgina no pot dir no ho diu
-- a mitges (docs/30, docs/60).
local OFFSETS = {}
for i = 1, (14 + 12) + 1 do
  local minutes = (i - 1) * 60 - 12 * 60
  local sign = (minutes < 0) and '-' or '+'
  local m = math.abs(minutes)
  OFFSETS[i] = string.format('%s%02d:%02d', sign, m // 60, m % 60)
end

local SPEEDS, SPEED_MHZ = { '80 MHz', '160 MHz', '240 MHz' }, { 80, 160, 240 }
-- **Les tres marxes del torn, amb nom i no amb un número** (0.189.107, l'amo: *«Número d'instruccions per cicle …
-- Cal fer mesures amb Lua per torn, donar velocitat lenta, normal, ràpida»*): a turn of the interpreter is the latency of the
-- glass and the keys, so a person chooses how long a finger may wait and not how many instructions the hook counts. The
-- milliseconds of each one are measured on this board when the tab is opened, and the numbers are the hook's own.
local TURNS, TURN_VALUE = { 'Slow', 'Normal', 'Fast' }, { 5000, 20000, 50000 }
local CURSORS, CURSOR_MS = { '600 ms', '850 ms', '1 100 ms' }, { 600, 850, 1100 }
local UNITS = { 'auto', 'sd', 'littlefs' }
-- **Els tres temps de quietud abans del son profund** (0.190.9, l'amo: *«afegir selector de temps per l'idle de settings->battery
-- (per fer el DeepSleep)»*): the seconds are the setting (`cfg.idle`) and the words are the app's, com les hores del rellotge.
local IDLE_SECONDS = { 300, 600, 900 }
local IDLE_WORDS = { '5 minutes', '10 minutes', '15 minutes' }
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
  -- **I el que el botó diu es veu** (0.189.143): the action of a button runs in the loop of the page, and nothing presented after
  -- it, so the line under the rows —the one that says what the page has just done— was written and never painted: it only came up
  -- when something else repainted the page, and the owner found it on the glass with the clock he had just set. A button that
  -- leaves no card of its own asks for the page; one that opens a card (the question of an erase) leaves the work for the next
  -- turn, and the card paints itself (docs/30, docs/45).
  one.on_click = function()
    wanted = function()
      on_click()
      if wanted == nil then settle() end
    end
  end
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
  -- **El nom també canvia**: a row that says the number it is talking about («a turn of 20 000») gets both from the same
  -- question, and the label is the one it was given when the answer has no name of its own (docs/45).
  refresh_of[one] = function()
    local title, detail = get_text()
    if title ~= nil then one.title = title end
    one.detail = detail or ''
  end
  named[name] = one
  rows[tab][#rows[tab] + 1] = one
  return one
end

-- **Quant costa un torn, mesurat i no copiat** (0.189.107, l'amo: *«Cal fer mesures amb Lua per torn»*): a lap of
-- `k = k + 1` is three instructions —ADDI, MMBINI and FORLOOP— and a known number of laps timed with `sys.time_ms()` says what
-- an instruction costs with the kernel's turn already inside it; `sys.turn()` says how many instructions a turn is worth, so
-- the milliseconds of one turn are that arithmetic and nothing else. The cheapest of three tries, because a page of ink that
-- is still going up only ever adds time (docs/75, docs/95, and `apps/luaperf` is the whole recipe).
local us_per_lap = nil

local function measure_a_lap()
  if us_per_lap ~= nil then return us_per_lap end
  local function spin(n)
    local k = 0
    for i = 1, n do k = k + 1 end
    return k
  end
  local best = nil
  for _ = 1, 3 do
    local laps = 8000
    local started = sys.time_ms()
    spin(laps)
    local ms = sys.time_ms() - started
    if ms > 0 then
      local us = ms * 1000 / laps
      if best == nil or us < best then best = us end
    end
  end
  us_per_lap = best
  return best
end

local function turn_ms()
  local us = measure_a_lap()
  if us == nil then return nil end
  return (sys.turn() / 3) * us / 1000
end

-- **Els tempos, de la taula de comptadors** (0.189.108, l'amo: *«si pot ser agafa de les estadístiques els tempos de les apps
-- lua, així es pot valorar quin es el numero mágic i quins valors son segurs per ambdos sistemes»*): `sys.stats(<paraula>)`
-- answers the calls and the microseconds of one meter and a page divides them, perquè el que decideix un torn és la mitjana i
-- no el total. `kernel.loop` és la volta sencera del sistema, `lua.run` el que hi ha de Lua a dins i `lua.call` una crida d'una
-- app al sistema (docs/30, docs/75, docs/95).
local function stats_of(word)
  if sys.stats == nil then return nil, nil end
  local calls, us = sys.stats(word)
  if calls == nil or calls == 0 then return nil, nil end
  return calls, us
end

local function mean_us(word)
  local calls, us = stats_of(word)
  if calls == nil then return nil end
  return us / calls
end

-- **El que un comptador costa per torn del sistema**: `lua.run` es compta una vegada per cada entrada a Lua i el que un torna a
-- pagar és el torn del nucli, o sigui que el que hi ha de Lua en un torn és el seu total entre els torns i no la seva pròpia
-- mitjana (docs/30, docs/95).
local function per_turn_us(word)
  local turns = stats_of('kernel.loop')
  local _, us = stats_of(word)
  if turns == nil or us == nil then return nil end
  return us / turns
end

-- **El que vol dir el número d'un torn, dit amb les paraules de l'amo** (0.189.107: *«el número son instruccions de la máquina
-- virtual LUA pot executar per torn»*): the line under the rows is the definition and the rows are the numbers —the slice in
-- force and what it costs here, the mean of a turn of the system and the part of it that is Lua—, o sigui que una persona
-- llegeix què és el número i què val abans de moure'l (docs/75, docs/95).
local kTurnNote = 'Instructions of the Lua VM in one turn'

-- **El torn sencer, i quina part se'n va esperant** (0.189.148): `kernel.loop` és la volta del sistema amb tot a dins
-- —l'app, els dispositius, el rellotge— i `queue.wait` és el tros que va passar aturat a la cua d'esdeveniments, que
-- també és dins del torn. El número sol enganya: en una placa quieta gairebé tot és espera, i per això el nucli va separar
-- els dos comptadors (`meter.h`, 0.189.85) —un torn de deu mil·lisegons és feina o és silenci, i són dues vides diferents
-- per a la placa— (docs/30, docs/95).
local function whole_turn_us()
  return mean_us('kernel.loop')
end

local function turn_wait_us()
  local turns = stats_of('kernel.loop')
  local wait_calls, wait_us = stats_of('queue.wait')
  if turns == nil or wait_calls == nil or wait_us == nil then return nil end
  return wait_us / turns
end

local function us_text(us)
  if us == nil then return '-' end
  if us >= 1000 then return string.format('%d ms', math.floor(us / 1000 + 0.5)) end
  return string.format('%d us', math.floor(us + 0.5))
end

-- **El torn, dit al registre amb quantes voltes s'ha mesurat** (0.189.148): the rows are read when the page is laid out, and
-- a board that opens straight onto this tab has taken one turn —so the number is a curiosity and not a measurement—; the line
-- under it says the count, which is the only thing that tells the two apart, and it is written when a person opens the tab
-- and not on every paint, because a line per turn is a console nobody reads (docs/30, docs/95).
local function note_the_turn()
  local calls = stats_of('kernel.loop') or 0
  -- **I al PC el torn és el del PC** (0.189.148): `kernel.loop` es mesura amb el rellotge de debò, i en aquesta máquina
  -- aquest rellotge és el del PC —de pressa— mentre que `lua.run` és el model del que costaria a la placa (`--lua-cpu`,
  -- seixanta nanosegons per instrucció). A la placa les dues són la mateixa moneda i el tros de Lua cap dins del torn; a
  -- l'emulador no, i per això la línia ho diu en comptes de deixar que la suma no quadri (docs/85, docs/95).
  console.print(string.format('settings: a whole turn is %s over %d %s, %s of it waiting%s',
                              us_text(whole_turn_us()), calls, (calls == 1) and 'turn' or 'turns',
                              us_text(turn_wait_us()),
                              sys.emulator and ' (the clock of this PC, not the board\'s)' or ''))
end

-- **Les velocitats que el governador pot fer servir**: the list the door answers, which is the same mask the policy reads.
local function allowed(mhz)
  local list = cfg.freq() or {}
  for _, one in ipairs(list) do
    if one == mhz then return true end
  end
  return false
end

local function index_of(list, value)
  for i, one in ipairs(list) do
    if one == value then return i end
  end
  return 0
end

-- ---------------------------------------------------------------- l'hora i la data
--
-- **L'època d'un moment local la fa el sistema** (0.189.130): `time.epoch` és la inversa de `time.date()` i porta l'offset de la
-- placa —l'hora del mapa i l'hora d'estiu—, que és el mateix número que fan servir la franja de la barra i la resta de pàgines.
-- Aquí hi havia l'aritmètica civil i l'offset del mapa a mà, i ignorava `time.dst()`: un rellotge posat des d'aquesta pàgina
-- amb l'estiu marcat quedava una hora desviat de la que deia la pàgina de la zona horària (docs/30, docs/60).
-- **I el dia que no existeix es diu com a tal** (0.189.142): la columna dels dies ofereix el trenta-u de tots els mesos, i el
-- sistema refusa el trenta-u de febrer i el dels mesos de trenta. Abans el número es convertia en el dos de març i la pàgina
-- deia el dia que la persona havia triat; ara torna `nil` i una frase, i una pàgina que digués «aquesta placa no té cap
-- calendari» estaria dient una mentida sobre un dia (docs/30, docs/60).
local function epoch_of(day, month, hour, minute)
  local today = time.date()
  if today == nil then return nil, 'This board has no calendar to write an hour into' end
  local at = time.epoch(today.year, month, day, hour, minute)
  if at == nil then return nil, string.format('%d %s does not exist', day, MONTHS[month]) end
  return at
end

-- **L'hora d'estiu és una hora i la diu un sol lloc** (docs/60): the box writes `time.dst` and the offset of the board is the
-- map's plus this one, exactly as `cfg::offset_minutes` counts it.
local kDaylightMinutes = 60

local function tz_text(minutes)
  local sign = (minutes < 0) and '-' or '+'
  local m = math.abs(minutes)
  return string.format('%s%02d:%02d', sign, m // 60, m % 60)
end

-- **La fila de la zona ensenya l'hora efectiva** (0.189.141): `time.offset()` és el número que el vidre ensenya a tot arreu
-- —la franja de la barra, la pàgina de la zona horària i aquesta fila—, i el que desa és `time.tz()`, el número del mapa.
local function tz_here() return tz_text(time.offset()) end

-- **I el número del mapa darrere d'un offset del vidre**: while the box of summer is ticked they are an hour apart, and the row
-- knows how much because it asks the same door the box writes. Fins a la 0.189.140 desava el que ensenyava, o sigui que acceptar
-- el valor que ja era a la pantalla movia el rellotge de la placa una hora més (docs/30, docs/60).
local function map_minutes(showing)
  return showing - (time.dst() and kDaylightMinutes or 0)
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
      -- **La mesura es fa quan algú mira el processador** (0.189.107): the milliseconds of a turn are what the numbers of
      -- the slice are worth on this board, so they are measured the first time the tab is opened and said in the line
      -- under the rows, which is where a person looks after moving something (docs/75, docs/95).
      if page == 2 then
        say(kTurnNote)
        note_the_turn()
      end
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
    local epoch, why_not = epoch_of(clock.day, clock.month, clock.hour, clock.minute)
    if epoch == nil then
      say(why_not)
      console.print('settings: the clock was not set: ' .. tostring(why_not))
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
  -- **La casella de l'estiu diu els dos números** (0.189.141): the zone of the map and the hour of the board, which are an hour
  -- apart while it is ticked, so the line under the rows and the row of the zone cannot disagree about what moved (docs/30).
  check_row(1, 'dst', 'Daylight saving time', function() return time.dst() end, function(on)
    time.dst(on)
    console.print(string.format('settings: daylight time %s, the zone is %s and the board shows %s',
                                on and 'on' or 'off', tz_text(time.tz()), tz_text(time.offset())))
  end)
  choice_row(1, 'tz', 'Time zone', OFFSETS, function() return index_of(OFFSETS, tz_here()) end, function(index)
    local text = OFFSETS[index]
    local sign = (text:sub(1, 1) == '-') and -1 or 1
    local h, m = text:match('^[+-](%d+):(%d+)$')
    if h == nil then return end
    -- **L'hora triada és la del vidre; la que es desa és la del mapa** (0.189.141): `kDaylightMinutes` comes out of what the
    -- person chose, because the box of summer has already written its own hour (docs/30, docs/60).
    local kept = map_minutes(sign * (tonumber(h) * 60 + tonumber(m)))
    time.tz(kept)
    console.print(string.format('settings: zone %s, and the board shows %s', tz_text(kept), tz_text(time.offset())))
  end)
  choice_row(1, 'week', 'First day', WEEKS, function() return (time.week() == 7) and 2 or 1 end,
             function(index) time.week((index == 2) and 7 or 1) end)
  check_row(1, 'leap', 'Leap year', function() return time.leap() end, function(on) time.leap(on) end)
  -- **El botó de l'hora espera la resposta i diu la veritat** (0.189.143): `net.ntp` is a promise of the kernel's queue like every
  -- other request of the radio, and this button was calling it **without `await`**: `pcall` answered `true` with the promise in
  -- its hand, so the page said *«The hour comes from the network»* on a board with no network saved —the commonest board there
  -- is— and the clock it promised had never been set. The shape is the one the page of the time zone has: the promise inside a
  -- `pcall` with a deadline, and the sentence for a person instead of the code of the kernel (docs/30, docs/60).
  button_row(1, 'ntp', 'Sync from the network', function()
    local why = 'this board has no radio'
    local ok = false
    if caps.net_wifi then
      local asked, answer = pcall(function() return net.ntp(nil, kSyncTimeoutMs):await() end)
      ok = asked and answer == true
      if not asked then
        local code = (type(answer) == 'table') and answer.code or nil
        if code == 'E_NONET' then
          why = 'no network saved'
        elseif code == 'E_TIMEOUT' then
          why = 'the server did not answer'
        elseif code == 'E_BUSY' then
          why = 'the radio is busy'
        else
          why = 'the request did not go out'
        end
      end
    end
    if ok then
      say('The hour comes from the network')
      console.print('settings: the time server set the clock')
    else
      say('No clock: ' .. why)
      console.print('settings: no clock: ' .. why)
    end
  end)

  -- ---- CPU: **la política, les tres velocitats i la marxa del torn** (0.189.107, l'amo: *«(desplegable) Velocitat …
  -- [X] Frequency scaling allowed: [X] 80Mhz [X] 160Mhz [X] 240Mhz»*). The switch is the policy —on, the load decides the
  -- step; off, the one below it— and the three boxes are the steps it may move between: a step that is not allowed is not
  -- taken even for a paint, which is what makes the choice worth having (docs/50, docs/60).
  check_row(2, 'autospeed', 'Automatic scaling', function() return power.cpu_auto() end,
            function(on) power.cpu_auto(on) end)
  for _, mhz in ipairs(SPEED_MHZ) do
    check_row(2, 'freq' .. mhz, 'Allow ' .. mhz .. ' MHz', function() return allowed(mhz) end,
              function(on) cfg.freq(mhz, on) end)
  end
  choice_row(2, 'speed', 'Speed', SPEEDS, function() return index_of(SPEED_MHZ, power.cpu()) end,
             function(index)
               power.cpu(SPEED_MHZ[index])
               say(kTurnNote)
             end)
  choice_row(2, 'turn', 'Lua a turn', TURNS, function() return index_of(TURN_VALUE, cfg.turn()) end,
             function(index)
               cfg.turn(TURN_VALUE[index])
               say(kTurnNote)
             end)
  -- **El que costa el torn que hi ha posat, mesurat aquí** (0.189.107): the slice is a number of instructions of the Lua VM and
  -- what a person needs to judge it is the milliseconds the glass and the keys wait for it, which is the slice divided by the
  -- price of an instruction on this board (docs/75, docs/95).
  read_row(2, 'cost', 'A turn', function()
    local ms = turn_ms()
    return string.format('A turn of %s', sys.turn()), (ms ~= nil) and string.format('%d ms', math.floor(ms + 0.5)) or '-'
  end)
  -- **I el que el sistema està fent de debò**: la mitjana d'un torn **sencer** del nucli i quantes n'hi ha hagut, que és el
  -- número que la fila de dalt només *pronostica*: allà el càlcul surt del preu d'una instrucció i de la llesca triada, i
  -- aquí surt del rellotge. Si els dos no s'assemblen, el que falla és el pronòstic o la placa, i una persona ho ha de
  -- poder veure (0.189.148, docs/30, docs/95).
  read_row(2, 'whole', 'A whole turn', function()
    local calls, us = stats_of('kernel.loop')
    if calls == nil then return nil, 'nothing counted' end
    -- **El número sol, i el compte al registre** (0.189.148): the row used to say `12, 338 us each`, and a row that says a count
    -- and a time side by side reads as one thousand two hundred and thirty-eight microseconds. What a person wants here is the
    -- time; the number of turns the mean is worth is in the line of the log and in `stats` (docs/30, docs/95).
    return nil, us_text(us / calls)
  end)
  -- **I quin tros d'aquell torn és espera i no feina** (0.189.148): `queue.wait` és la estona aturada a la cua, que el
  -- rellotge del torn també compta. Sense aquesta fila, «a whole turn: 10 ms» es llegeix com una CPU saturada quan és una
  -- placa esperant una tecla; amb ella, el tros de treball és el que queda (docs/30, docs/95).
  read_row(2, 'waiting', 'Of that, waiting', function()
    local us = turn_wait_us()
    if us == nil then return nil, '-' end
    return nil, us_text(us)
  end)
  read_row(2, 'lua', 'Of that, Lua', function()
    local us = per_turn_us('lua.run')
    if us == nil then return nil, '-' end
    return nil, us_text(us)
  end)

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
  -- **On viuen les dades de les apps, que és el disc preferit de dalt** (0.190.8, l'amo: *«perqué es perden les dades d'historic
  -- de Battery i TimeClock al pujar un firmware nou? … i tinc SD»*): `/data/<id>/` era el flaix sempre i `-t uploadfs` reescriu
  -- la partició sencera, i ara segueix la mateixa tria que `/cfg` — una sola preferència, perquè dues podrien dir coses
  -- diferents i perquè la Paper no té DRAM per a dues respostes. Aquesta fila **llegeix el muntatge** i no una porta de `cfg`:
  -- el que ensenya és on ha quedat de debò, que és el que una persona vol saber abans de flashejar (docs/30, docs/70, docs/80).
  read_row(3, 'ground', 'Apps data', function()
    for _, one in ipairs(fs.mounts() or {}) do
      if one.path == '/data' then
        return nil, (one.type == 'sd') and 'the card' or 'the flash'
      end
    end
    return nil, 'nowhere'
  end)
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

  -- ---- System: **les tres apps que el sistema obre per una persona** (0.189.109, la tercera a 0.189.150): the one that comes
  -- up with the board (`cfg.home`), **the one the gear of the panel opens** (`cfg.settings`, l'amo: *«Afegeix un selector
  -- d'aplicació per gestionar els settings … a la pestanya "System" on posa "App at start" doncs igual»*) i **la que tapa el
  -- vidre quan el sistema es bloqueja** (`cfg.lock`). The list is the apps of this disk and `none` for all three, which are the
  -- same door and the same words (docs/30, docs/40).
  -- **La llista d'apps d'aquest disc, i les dues de sota són la mateixa porta** (0.189.109): `none` primer —una app que no hi és
  -- és una tria i no un forat— i després tot el que el disc té. **L'app dels ajustos no s'ofereix com a app d'inici** (una pàgina
  -- que obre els ajustos no és la pàgina amb què arrenca una placa), però sí que és a la llista del botó, que és el seu lloc.
  local every = {}
  local ok, mine = pcall(apps.list)
  if ok and type(mine) == 'table' then
    for _, id in ipairs(mine) do every[#every + 1] = id end
  end
  local list = { 'none' }
  for _, id in ipairs(every) do
    if id ~= 'settings' then list[#list + 1] = id end
  end
  -- **Els tres noms que l'amo va demanar** (0.189.150): `Home` és l'app de l'inici, `Settings` la del botó del panell i **`Lock`
  -- la que tapa el vidre**. The names are the owner's and the value is the app's, so a page that says `Home` is saying what
  -- the setting is and not the name of a variable (docs/30, docs/45).
  choice_row(5, 'home', 'Home', list, function() return index_of(list, cfg.home() or 'none') end,
             function(index) cfg.home(list[index]) end)
  local gears = { 'none' }
  for _, id in ipairs(every) do gears[#gears + 1] = id end
  choice_row(5, 'gear', 'Settings', gears, function()
    local id = cfg.settings() or ''
    if id == '' then return 1 end
    return index_of(gears, id)
  end, function(index)
    cfg.settings((index == 1) and 'none' or gears[index])
  end)
  local locks = { 'none' }
  for _, id in ipairs(every) do locks[#locks + 1] = id end
  choice_row(5, 'lock', 'Lock', locks, function()
    local id = cfg.lock() or ''
    if id == '' then return 1 end
    return index_of(locks, id)
  end, function(index)
    cfg.lock((index == 1) and 'none' or locks[index])
  end)

  -- ---- Battery: **el que la placa sap de la seva pila**, que és un número i no un ajust d'aquesta pàgina (docs/30). Les
  -- files són del sistema i es tornen a llegir cada cop que la pestanya es col·loca.
  read_row(6, 'battery', 'Battery', function()
    local ok2, one = pcall(power.battery)
    if ok2 and type(one) == 'table' then
      return string.format('%d%% of %d mV%s', one.pct or 0, one.mv or 0, one.charging and ', charging' or '')
    end
    return 'this board does not know'
  end)
  read_row(6, 'board', 'Board', function() return string.format('%s, %d MHz', sys.board, power.cpu()) end)
  -- ---- **I el son de la placa, aquí** (0.190.5, l'amo: *«has posat l'opció a Settings->Battery ?»*): the switch of the board's power
  -- lives with what the battery knows —a person who opens Battery is asking what the board does with its charge— and not with the three
  -- apps the system opens on System. The board powers itself off until its next alarm when nobody has touched it for the time of
  -- *Sleep after* —no console, no finger on the glass and no key of the dial—, which is law 9 and what makes the quartz count the hour. **Una persona que vol la
  -- placa desperta** —una demo, una sessió de mesura, una app que escriu un registre— ho apaga aquí, i la resposta es queda
  -- (`cfg.sleep`) perquè el pròxim boot no torni a la política del sistema. El que la fila ensenya és el que la política té, no el que
  -- l'app ha demanat (docs/30, docs/45).
  -- **I el dit com a porta** (0.190.16, l'amo: *«opcionalment el panell tàctil (per defecte false) així no cal mantenir despert el
  -- panell»*): a finger on the glass wakes the board from a deep sleep, i ve apagat de sèrie perquè sentir un dit vol dir deixar el rail
  -- del panell i el controlador del tàctil desperts tota la son. El que la fila ensenya és el que el sistema ha acceptat (docs/30, docs/45).
  -- **I la fila només hi és on la placa hi pot sentir** (0.190.17, l'amo: *«a koala, atom i matri[x] simplement s'ignora el paràmetre»*): a
  -- board with no touch panel keeps the setting and does nothing with it, so a switch that says nothing would be a door to nowhere. The
  -- question is `caps.touch` and not the value, perquè el que decideix si la fila té feina és el maquinari (docs/30, docs/50).
  if caps.touch then
    check_row(6, 'touchwake', 'Wake on touch', function() return cfg.touch_wake() == true end, function(on)
      cfg.touch_wake(on)
      say(on and 'A finger on the glass wakes the board' or 'The glass stays asleep with the board')
      console.print(string.format('settings: touch to wake is %s', on and 'on' or 'off'))
    end)
  end
  check_row(6, 'sleep', 'Sleep when idle', function() return cfg.sleep() == true end, function(on)
    cfg.sleep(on)
    say(on and 'The board sleeps when nobody uses it' or 'The board stays awake')
    console.print(string.format('settings: the idle sleep is %s', on and 'on' or 'off'))
  end)
  -- **I quanta quietud és «inactiu»** (0.190.9, l'amo: *«un selector desplegable … per seleccionar quan es posa en DeepSleep»*):
  -- the same door the minutes of the lock use —a `ui.selector` that opens the card of the system with the three times— i
  -- `cfg.idle` el número que la política llegeix abans d'apagar la placa. Tres i no més —**cinc minuts (el de sèrie), deu i quinze**,
  -- els mateixos tres del pany—, i el que la fila ensenya és el que el sistema ha acceptat (docs/30, docs/45, docs/60).
  choice_row(6, 'idle', 'Sleep after', IDLE_WORDS, function() return index_of(IDLE_SECONDS, cfg.idle()) end,
             function(index) cfg.idle(IDLE_SECONDS[index]) end)

  -- ---- Privacy: **el bloqueig de la pantalla, que és una app i no una barra que desapareix** (0.189.150, l'amo: *«El Toggle
  -- de la pestanya "Privacy" activa que es llenci l'app escollida com lock al inici del sistema i al despertar d'un sleep.
  -- Afegeix també un selector de temps al panell Privacy "Lock after:" 5, 10, 15 minutes, per defecte 10 minutes»*). The switch
  -- writes `cfg.lockon` i el desplegable els minuts; l'app que s'obre és la fila `Lock` de la pestanya System, i si no n'hi ha
  -- cap el commutador no té res a obrir i ho diu en comptes d'engegar una pàgina que no existeix (docs/30, docs/45, docs/60).
  check_row(7, 'lockon', 'Lock the screen', function() return cfg.lock_on() == true end, function(on)
    cfg.lock_on(on)
    local page = cfg.lock() or ''
    if on and page == '' then
      say('No lock app: choose one in System')
      console.print('settings: the lock is on and no page is chosen (`cfg lock <id>`)')
    else
      say(on and 'The screen locks at the start, after a sleep and when idle' or '')
    end
  end)
  -- **Els minuts sense tocar res**, amb els tres de la llista i el deu de sèrie: `cfg.lock_after` els desa i el nucli els
  -- compta amb el rellotge de les durades, no amb l'hora (docs/30, docs/60).
  local LOCK_AFTER = { '5 minutes', '10 minutes', '15 minutes' }
  local LOCK_AFTER_MIN = { 5, 10, 15 }
  choice_row(7, 'lockafter', 'Lock after', LOCK_AFTER, function()
    return index_of(LOCK_AFTER_MIN, cfg.lock_after() or 10)
  end, function(index) cfg.lock_after(LOCK_AFTER_MIN[index]) end)

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
-- **I si la pàgina s'obre ja al processador, el torn es diu igual** (0.189.148): the tab that was left is the tab that comes
-- back (the `state` session), o sigui que el camí del desplegable no és l'únic que hi arriba (docs/30, docs/95).
if page == 2 then note_the_turn() end

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
