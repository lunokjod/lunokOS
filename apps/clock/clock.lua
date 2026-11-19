-- **Clock**: el rellotge del projecte del paper, amb les seves quatre pestanyes.
--
-- Port de `OLD/PaperOS/data/app/Clock/` (`pos_launch.lua`, `dial.lua` i `alarms.lua`) i ha de ser **la mateixa app**: les
-- mateixes quatre pestanyes —Time, Alarm, Timer i Chrono—, la mateixa esfera grisa amb els seixanta tics, les mateixes
-- dues frases, els mateixos botons i els mateixos textos de cada pantalla, a les mateixes mides (lletra petita de 33
-- píxels de línia, cos de 43 i número gran de 82, que aquí són els passos 3, 4 i 7 d'aquesta cara).
--
-- El que canvia és el que toca el sistema i prou: `paperos.ui.tabs()` és aquí `ui.tabs`, els `selector` i els `button`
-- són els d'aquest nucli, la llista d'alarmes és un `ui.list` de files amb el seu toc (el pacte de PaperOS: el dit tria
-- una fila i la porta al formulari), i les alarmes es desen en un fitxer de l'app i s'armen amb `alarm.at` —que és el
-- mateix pacte: una promesa viva per a la propera alarma i la llista sencera al disc.
--
-- **L'esfera és l'única cosa que no es pot fer amb un control**, i és el dibuix que més vegades va haver de canviar a
-- PaperOS: el to 10 i no un altre (és un dels quatre nivells que la passada barata pot dibuixar), l'anell amb el mateix
-- gruix a tot arreu —una anella i no una banda, o a les dotze queda un pèl—, els seixanta tics pengen de la vora de dins
-- de l'anell, les dotze marques amb la de l'hora més gruixuda, i les busques amb cua i amb un tap al mig. Tot surt del
-- costat curt de la caixa, o sigui que el mateix dibuix és igual amb el panell tombat.

local TABS = { 'Time', 'Alarm', 'Timer', 'Chrono' }

-- **Les tres mides de lletra de l'app del paper i els passos d'aquesta cara**: ui28b (33 píxels de línia), ui36b (43) i
-- ui70b (82), que amb la cara del disc són els passos 3, 4 i 7 i amb la de cinc per set són uns altres. El número no
-- s'escriu: es pregunta, perquè una app que porta la mida de la lletra a dins és una app que es trenca el dia que la
-- cara canvia (docs/30, docs/92).
local function step_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end
  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end
  return scale
end

local kSmall, kBody, kBig = step_for(33), step_for(43), step_for(82)

-- **El to de l'esfera és una decisió de la forma d'ona i no de gust**: el 10 és un dels quatre nivells que la passada
-- barata del panell pot dibuixar, i per això la cara es pot repintar cada minut sense haver de rentar el vidre. Les
-- busques i el número són tinta: l'esfera és l'escenari i el que es llegeix és el negre.
local FACE, INK = 10, 0

-- **Les dues qualitats que aquesta app nomena**: la cara de l'esfera porta un gris, o sigui que la tria el sistema
-- (`gl16`/`gc16`, docs/30), i una línia de tinta sobre paper es pot pintar amb la més barata de totes.
local kCheapMode = 'du'
local kCleanMode = 'gc16'
-- **Setze nivells i cap flaix, i el paper es queda paper**: el `du` és el més barat però deixa cinc setzens de la xifra
-- anterior —el número s'acaba llegint doble—, i la qualitat que tria el sistema per a una caixa de només tinta i paper és
-- la de dos nivells, que en aquest vidre surt com una banda grisa al voltant de les xifres (vist a l'emulador).
local kSoftMode = 'gld16'

local MARGIN, GAP, TABS_H = 12, 8, 63
local ROW_H, MAX_ROWS = 56, 4

local QUARTERS = { '00', '15', '30', '45' }
local WEEKDAYS = { 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday', 'Sunday' }

local DAYS, HOURS, MINUTES, SECONDS = {}, {}, {}, {}
for i = 1, 31 do DAYS[i] = string.format('%d', i) end
for i = 0, 23 do HOURS[i + 1] = string.format('%02d', i) end
for i = 0, 59 do
  MINUTES[i + 1] = string.format('%02d', i)
  SECONDS[i + 1] = string.format('%02d', i)
end

-- ---------------------------------------------------------------- l'estat

local page = 1
local hint = ''

-- La llista d'alarmes: `{h, m, note}` per a una de diària i `{h, m, note, d, mo}` per a una amb data.
local alarms = {}
local marked = 0

-- **El compte enrere i el cronòmetre són dos rellotges i no dos números de la pàgina** (l'amo: *«Clock -> Chronomether, no
-- manté l'hora ni continua comptant rera tancar l'app o el reinici … se suposa que aquestes apps usaven el disc i els
-- rellotges que puguin haver»*). La tirada es mesura amb `sys.time_ms()` —que no salta mai, i NTP pot moure el segon— i
-- s'ancora a l'hora de debò amb `time.now_ms()`, que és la de la pólvora de la placa i sobreviu a un reinici: els dos junts
-- donen un cronòmetre que compta mentre l'app no hi és (docs/30, docs/60).
-- **Els dos camps del compte enrere es declaren aquí i no amb els altres controls** perquè el fitxer del que corre els
-- llegeix i els escriu, i un `local` que encara no existeix és un global buit dins d'una funció que ja s'ha escrit.
local minSel, secSel
local counting, leftMs, leftFrom, leftAt = false, 0, 0, 0
local chronoRun, chronoAcc, chronoFrom, chronoAt, chronoWhy = false, 0, 0, 0, ''
local timerWhy = ''

-- L'alarma que ha sonat, si n'hi ha una: mentre hi és, la pantalla és aquesta i prou.
local fired = nil

local box = {}

-- ---------------------------------------------------------------- el disc

local function store_path() return fs.data('alarms.txt') end

local function read_alarms()
  alarms = {}
  local path = store_path()
  if path == nil then return end
  local text = fs.read(path)
  if text == nil then return end
  for row in text:gmatch('[^\n]+') do
    local mo, d, h, m, note = row:match('^(%d%d)%-(%d%d) (%d%d):(%d%d)\t(.*)$')
    if mo == nil then
      h, m, note = row:match('^(%d%d):(%d%d)\t(.*)$')
      if h ~= nil then alarms[#alarms + 1] = { h = tonumber(h), m = tonumber(m), note = note } end
    else
      alarms[#alarms + 1] = { h = tonumber(h), m = tonumber(m), note = note, d = tonumber(d), mo = tonumber(mo) }
    end
  end
end

local function write_alarms()
  local path = store_path()
  if path == nil then return end
  local lines = {}
  for _, a in ipairs(alarms) do
    if a.d ~= nil and a.mo ~= nil then
      lines[#lines + 1] = string.format('%02d-%02d %02d:%02d\t%s', a.mo, a.d, a.h, a.m, a.note or '')
    else
      lines[#lines + 1] = string.format('%02d:%02d\t%s', a.h, a.m, a.note or '')
    end
  end
  if #lines == 0 then lines[1] = '' end
  fs.write(path, table.concat(lines, '\n') .. '\n')
end

-- ============================================================ el que corre
--
-- **El que corre es desa amb l'hora de la placa i no amb la de l'arrencada**: `sys.time_ms()` mesura la tirada d'aquest
-- arrencada i `time.now_ms()` diu en quin moment del món va començar, o sigui que el que s'escriu al disc és
-- **el que ja comptava més el moment en què va arrencar**, i d'aquí es torna a calcular quan l'app torna. Un cronòmetre que
-- s'atura en tancar l'app no és un cronòmetre, i un compte enrere que s'atura tampoc (docs/30, docs/45).
--
--   chrono	run|hold	12345	1763234567890
--   timer	run|hold	600000	1763234567890
--   pick	05	30
--
-- **Aquest fitxer és dada de l'app i no una sessió `state`**: una sessió és la forma de la vista, i un número que corre no
-- és una forma (docs/30, la norma de la sessió).
local runPick = nil    -- the countdown the person had picked, until the two fields exist

local function epoch_ms()
  local ok, now = pcall(time.now_ms)
  if not ok or type(now) ~= 'number' or now <= 0 then return 0 end
  return math.floor(now)
end

local function run_path() return fs.data('run.txt') end

local function write_run()
  local path = run_path()
  if path == nil then return end
  local min = (minSel ~= nil) and math.max(0, minSel.index - 1) or 0
  local sec = (secSel ~= nil) and math.max(0, secSel.index - 1) or 0
  local text = string.format('chrono\t%s\t%d\t%d\ntimer\t%s\t%d\t%d\npick\t%d\t%d\n',
    chronoRun and 'run' or 'hold', chronoAcc, chronoRun and chronoAt or 0,
    counting and 'run' or 'hold', math.floor(leftMs), counting and leftAt or 0, min, sec)
  local written, why = fs.write(path, text)
  if written == nil then
    console.print('clock: the run is not on the disk (' .. tostring(why and why.code or 'the disk') .. ')')
  end
end

-- **La tornada**: el que el disc diu es converteix en el que l'app ensenya. La diferència de rellotge es comprova abans de
-- fer-la servir —una placa sense hora, o un rellotge que ha anat enrere, no poden inventar-se una tirada— i quan no es pot
-- saber, l'app ho diu a la línia de sota en lloc de mentir.
local function read_run()
  local path = run_path()
  if path == nil then return end
  local text = fs.read(path)
  if text == nil then return end
  local now = epoch_ms()
  for row in text:gmatch('[^\n]+') do
    local pick_min, pick_sec = row:match('^pick\t(%d+)\t(%d+)$')
    if pick_min ~= nil then
      -- **Els camps encara no existeixen quan això es llegeix**: the run file is read before the page is built, so the pick
      -- waits for the two fields and is put there once they are (docs/30).
      runPick = { min = tonumber(pick_min), sec = tonumber(pick_sec) }
    end
    local kind, mode, value, at = row:match('^([%a]+)\t(%a+)\t(%d+)\t(%d+)$')
    if kind == 'chrono' then
      local acc = tonumber(value) or 0
      if mode == 'run' then
        local started = (now > 0 and tonumber(at) ~= nil and tonumber(at) > 0) and tonumber(at) or 0
        local delta = (started > 0) and (now - started) or 0
        if delta > 0 then
          chronoAcc = acc + delta
          -- **L'ancora passa a ser aquest moment**: the value on the disk is the one the run started with, and what is on the
          -- page now is that value plus the time it ran, so the pair has to move together or the next boot counts it twice
          -- (docs/30).
          chronoAt = now
        else
          chronoAcc = acc
          chronoAt = 0
          chronoWhy = 'the clock is not set: the chronograph lost the time it ran while the app was away'
          console.print('clock: the chronograph is back without the time it ran (' .. chronoWhy .. ')')
        end
        chronoRun = true
        chronoFrom = sys.time_ms()
      else
        chronoAcc = acc
        chronoRun = false
      end
    elseif kind == 'timer' then
      local left = tonumber(value) or 0
      if mode == 'run' then
        local started = (now > 0 and tonumber(at) ~= nil and tonumber(at) > 0) and tonumber(at) or 0
        local delta = (started > 0) and (now - started) or 0
        left = left - delta
        if delta <= 0 then
          timerWhy = 'the clock is not set: the countdown lost the time it ran while the app was away'
        end
        if left <= 0 then
          counting = false
          leftMs = 0
          leftAt = 0
          if delta > 0 then
            timerWhy = 'the countdown ran out while the app was away'
            console.print('clock: the countdown ran out while the app was away')
          end
        else
          counting = true
          leftMs = left
          leftFrom = sys.time_ms()
          leftAt = now
        end
      else
        counting = false
        leftMs = (left > 0) and left or 0
      end
    end
  end
end

-- La fila d'una alarma, com la de la llista de PaperOS: la data si en té, l'hora i la nota.
local function row_text(a)
  if a.d ~= nil and a.mo ~= nil then
    return string.format('%02d/%02d %02d:%02d', a.d, a.mo, a.h, a.m), a.note or ''
  end
  return string.format('%02d:%02d', a.h, a.m), a.note or ''
end

-- **Quants minuts falten per a aquesta alarma**, comptats en el dia i no en una data: una alarma diària és la propera
-- hora d'aquell dia, i una amb data és la d'aquell dia de l'any —si ja ha passat, la de l'any que ve—.
local function minutes_to(a, now)
  local target = a.h * 60 + a.m
  local today = now.hour * 60 + now.min
  if a.d == nil or a.mo == nil then
    local delta = target - today
    if delta <= 0 then delta = delta + 1440 end
    return delta
  end
  local day = a.d - now.day
  local month = a.mo - now.month
  local delta = (day + month * 31) * 1440 + (target - today)
  if delta <= 0 then delta = delta + 365 * 1440 end
  return delta
end

local function sort_alarms()
  local now = time.date()
  if now == nil then return end
  table.sort(alarms, function(a, b) return minutes_to(a, now) < minutes_to(b, now) end)
end

-- Les alarmes viuen al disc i el sistema n'ha de portar **una**: la propera. Quan sona, `on_alarm` torna a armar-ne una
-- altra, que és el pacte de `alarm.at` d'aquest nucli.
local armed = 0

local function arm_next()
  if armed ~= 0 then
    alarm.cancel(armed)
    armed = 0
  end
  sort_alarms()
  local a = alarms[1]
  local now = time.date()
  if a == nil or now == nil then return end
  local ms = minutes_to(a, now) * 60000
  if ms <= 0 then return end
  -- **El payload és el text de la fila i no una data**: és el que la pantalla gran escriu quan sona, i així el que
  -- s'ha desat i el que es veu són la mateixa cosa (docs/30).
  -- **I `persist`, o la placa se l'endú a la tomba** (0.190.19, l'amo: *«L'app Clock posa alarmes? I fa raise … quan una alarma ha
  -- vençut?»*): a deep sleep d'aquesta placa és un reset, i la cua que torna del disc només porta les alarmes que van dir que sí —les de
  -- `battery`, `meteo` i `agenda` ho fan—. Una alarma diària sense `persist` s'armava, la placa s'adormia esperant-la... i no hi era.
  local spec = { in_ms = ms, raise = true, persist = true, payload = (row_text(a)) }
  -- **`repeat` és una paraula de Lua** i un camp escrit amb el seu nom ni tan sols s'analitza: es llegeix i s'escriu
  -- amb l'índex (docs/30).
  if a.d == nil then spec['repeat'] = 86400 end
  -- **Una alarma que no es pot armar no és una app morta**: la cua del sistema és finita i diu que no, i el que ha de
  -- passar és que es digui a la consola i la llista quedi com està (docs/30, docs/60).
  local ok, id = pcall(alarm.at, spec)
  if ok and type(id) == 'number' then
    armed = id
  else
    armed = 0
    console.print('clock: the next alarm is not armed (' .. tostring(ok and id or id.code or id) .. ')')
  end
end

-- ---------------------------------------------------------------- el dibuix

local function at_angle(cx, cy, r, deg)
  local a = math.rad(deg - 90)
  return math.floor(cx + r * math.cos(a) + 0.5), math.floor(cy + r * math.sin(a) + 0.5)
end

-- **Un traç de gruix `t`**: aquest canvas no té línies amb gruix, i el que fa un llapis és passar-hi un disc de la mida
-- del gruix al llarg del camí. Els caps surten rodons, que és com els té la vista de PaperOS.
local function brush_line(x0, y0, x1, y1, t, level)
  local steps = math.max(math.abs(x1 - x0), math.abs(y1 - y0))
  local r = math.max(1, t // 2)
  if steps <= 0 then
    ui.disc(x0, y0, r, level)
    return
  end
  for i = 0, steps do
    local k = i / steps
    ui.disc(math.floor(x0 + (x1 - x0) * k + 0.5), math.floor(y0 + (y1 - y0) * k + 0.5), r, level)
  end
end

-- **L'anell, i aquesta és la diferència entre una esfera nítida i un pèl a les dotze**: cada fila s'omple de la vora de
-- fora cap a dins, o sigui que el gruix és una distància al cercle i no un comptador de píxels, i surt el mateix a tots
-- els costats.
local function annulus(cx, cy, r, thick, level)
  if r < 8 then return end
  local inner = r - thick
  for dy = -r, r do
    local dx = math.floor(math.sqrt(math.max(0, r * r - dy * dy)) + 0.5)
    if dy > -inner and dy < inner then
      local din = math.floor(math.sqrt(math.max(0, inner * inner - dy * dy)) + 0.5)
      if dx > din then
        ui.rect(cx - dx, cy + dy, dx - din, 1, level)
        ui.rect(cx + din + 1, cy + dy, dx - din, 1, level)
      end
    else
      ui.rect(cx - dx, cy + dy, dx * 2 + 1, 1, level)
    end
  end
end

-- **L'esfera sencera**, amb l'ordre de PaperOS: el cercle, els tics, les dotze marques, les busques i el número al
-- damunt de tot —el número va últim perquè es llegeixi i no s'endevini—.
local function paint_face(x, y, w, h, now)
  local side = math.min(w, h)
  if side < 80 then return end
  local r = side // 2 - 10
  local cx = x + w // 2
  local cy = y + h // 2
  local ring = math.max(7, r // 24)

  annulus(cx, cy, r, ring, FACE)
  annulus(cx, cy, math.floor(r * 0.80), math.max(3, r // 60), FACE)

  local function centred(text, dy, scale)
    local tw = ui.text_width(text, scale)
    ui.text(cx - tw // 2, cy + dy, text, scale, INK)
  end

  if now == nil then
    centred('--:--', -44, kBig)
    centred('The clock is not set yet: it sets itself from the network', 46, kSmall)
    return
  end

  -- Els seixanta tics, curts i penjant de la vora de dins de l'anell.
  local len = math.max(6, r // 20)
  local out = r - ring - 1
  for i = 0, 59 do
    if (i % 5) ~= 0 then
      local x0, y0 = at_angle(cx, cy, out, i * 6)
      local x1, y1 = at_angle(cx, cy, out - len, i * 6)
      brush_line(x0, y0, x1, y1, 2, FACE)
    end
  end

  -- Les dotze marques: quatre de gruix per a una, sis per a un quart i nou per a l'hora on és la busca.
  local mark_out = r - ring + 2
  for i = 0, 11 do
    local quarter = (i % 3) == 0
    local length = quarter and math.floor(r / 5) or math.floor(r / 9)
    local thick = (i == (now.hour % 12)) and 9 or (quarter and 6 or 4)
    local x0, y0 = at_angle(cx, cy, mark_out, i * 30)
    local x1, y1 = at_angle(cx, cy, mark_out - length, i * 30)
    brush_line(x0, y0, x1, y1, thick, FACE)
  end

  -- Les busques: la de les hores curta i gruixuda, la dels minuts llarga, la cua darrere i el tap al mig.
  local hx, hy = at_angle(cx, cy, math.floor(r * 0.50), (now.hour % 12) * 30 + now.min * 0.5)
  local mx, my = at_angle(cx, cy, math.floor(r * 0.80), now.min * 6)
  local tx, ty = at_angle(cx, cy, -math.floor(r * 0.13), now.min * 6)
  brush_line(tx, ty, cx, cy, 8, INK)
  brush_line(cx, cy, mx, my, 8, INK)
  brush_line(cx, cy, hx, hy, 14, INK)
  ui.disc(cx, cy, math.max(8, r // 16), INK)

  centred(string.format('%02d:%02d', now.hour, now.min), -44, kBig)
  local date = string.format('%s %04d-%02d-%02d', WEEKDAYS[now.wday] or '', now.year, now.month, now.day)
  centred(date, 46, kSmall)
end

-- ---------------------------------------------------------------- el flux

-- **Les quatre pantalles són un sol flux vertical**, com l'arbre de PaperOS: la tira de pestanyes, la caixa que es menja
-- el lloc que sobra (l'esfera a Time, la llista a Alarm), la línia gran, la nota, els camps, la caixa de text i els
-- botons. El que no és d'aquesta pestanya no es veu i no ocupa lloc, que és el que fa que el formulari de l'alarma quedi
-- a baix i la llista a dalt sense cap número escrit a mà.
local function wrapped(text, width, scale)
  local lines, line = {}, ''
  for word in text:gmatch('%S+') do
    local next_line = (line == '') and word or (line .. ' ' .. word)
    if line ~= '' and ui.text_width(next_line, scale) > width then
      lines[#lines + 1] = line
      line = word
    else
      line = next_line
    end
  end
  if line ~= '' then lines[#lines + 1] = line end
  return lines
end

-- ============================================================ la sessió `state`
--
-- **La forma de la vista i res més** (0.189.12, docs/30): la pestanya on era, la fila marcada de la llista i el que hi ha
-- escrit a la nota. El compte enrere i el cronòmetre no hi són perquè no són forma: un número que corre és un fet de la
-- sessió i no una vista, i l'app el torna a fer amb el rellotge del sistema. El bloc és el que es copia de docs/30.
local kStateBytes = 1024
local kLayout = 'v1'      -- puja quan un control canvia de nom o la pàgina canvia de forma

local session = nil
local function the_state()
  if session == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    session = block
  end
  return session
end

local function escape(text)
  return (tostring(text or ''):gsub('\\', '\\\\'):gsub('\n', '\\n'))
end

local function unescape(text)
  return (text:gsub('\\(.)', function(c) return (c == 'n') and '\n' or c end))
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'focus', 'page', 'tab', 'scroll', 'marked' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
  end
  for name, value in pairs(now.check or {}) do
    lines[#lines + 1] = 'check:' .. name .. '=' .. (value and '1' or '0')
  end
  for name, value in pairs(now.text or {}) do
    lines[#lines + 1] = 'text:' .. name .. '=' .. escape(value)
  end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  local zero = body:find('\0', 1, true)
  if zero ~= nil then body = body:sub(1, zero - 1) end
  local now = { check = {}, text = {} }
  local first = true
  for line_in in body:gmatch('[^\n]+') do
    if first then
      first = false
      if line_in ~= kLayout then return nil end
    elseif line_in:sub(1, 6) == 'check:' then
      local name, value = line_in:match('^check:([^=]+)=(%d)$')
      if name ~= nil then now.check[name] = (value == '1') end
    elseif line_in:sub(1, 5) == 'text:' then
      local name, value = line_in:match('^text:([^=]+)=(.*)$')
      if name ~= nil then now.text[name] = unescape(value) end
    else
      local key, value = line_in:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end
  if first then return nil end
  return now
end

-- ============================================================ els controls
--
-- **Cada control es fa una vegada i es mou** (docs/30, docs/50): una pàgina que els fa i els desfà a cada canvi de
-- pestanya és una pàgina que parpelleja, i el que canvia entre pestanyes és on són i què diuen, no quins són.
local kField, kButton, kBox = 56, 63, 48

local function line(sc) return ui.line_height(sc) end

local function refused(what, why)
  console.print('clock: no ' .. what .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
end

local tabs, primary, secondary, dismiss, alarmList
local daySel, moSel, hourSel, quarSel, noteBox

-- El que té el cercle va pel nom i no pel número (docs/30), o sigui que l'app ha de saber quin és.
local shape = { focus = nil }

local function save_form()
  save_state({
    tab = page,
    marked = (marked > 0) and marked or nil,
    focus = shape.focus,
    text = { note = (noteBox ~= nil) and noteBox.text or '' },
  })
end

local function remember(on, name)
  if not on then return end
  shape.focus = name
  save_form()
end

local function placed(x, y, w, h)
  return { x = x, y = y, w = w, h = h }
end


-- ============================================================ el que diu la pàgina
local wid, hei = 0, 0

local function measure()
  local v = ui.caps()
  wid, hei = v.width, v.height
end

local function mmss(ms)
  if ms < 0 then ms = 0 end
  local total = math.floor(ms / 1000)
  return string.format('%02d:%02d', math.floor(total / 60), total % 60)
end

-- **El compte enrere no és un bucle que baixa**: és una data i una resta, o sigui que queda bé encara que l'app no hagi
-- corregut durant una estona. El cronòmetre és la mateixa aritmètica a l'altre costat (l'uptime de l'última arrencada
-- més el que ja comptava), i la diferència negativa no es tapa: vol dir que la placa es va reiniciar i es diu.
local function left_now()
  if not counting then return leftMs end
  local left = leftMs - (sys.time_ms() - leftFrom)
  return (left > 0) and left or 0
end

local function chrono_now()
  if not chronoRun then return chronoAcc end
  local run = sys.time_ms() - chronoFrom
  if run < 0 or run > 100000000 then run = 0 end
  return chronoAcc + run
end

-- **Segons i res més fi, i és una decisió del paper**: una centèsima no es pot ensenyar en aquest panell i cada segon que
-- canvia és una passada. Per sota de l'hora, els dos camps del compte enrere; a partir d'aquí, el tercer, perquè una hora
-- de cronòmetre passa.
local function chrono_text(ms)
  if ms < 0 then ms = 0 end
  local total = math.floor(ms / 1000)
  if total >= 3600 then
    return string.format('%d:%02d:%02d', math.floor(total / 3600), math.floor(total / 60) % 60, total % 60)
  end
  return mmss(ms)
end

-- **El que diu la línia de sota**, que és el manual d'aquesta app: cada pestanya té la seva frase i totes són les de
-- PaperOS, paraula per paraula (docs/45).
local function note_text()
  if fired ~= nil then return fired.note or '' end
  if hint ~= '' then return hint end
  if page == 1 then
    return (time.date() == nil) and 'The clock is not set yet: it sets itself from the network' or ''
  end
  if page == 2 then
    if #alarms == 0 then return 'no alarms. Pick the hour and the quarter, then Add' end
    sort_alarms()
    local extra = #alarms - MAX_ROWS
    local head = (extra > 0)
      and string.format('%d alarms, %d more are not listed', #alarms, extra)
      or string.format('%d alarm(s)', #alarms)
    return head .. ', next ' .. row_text(alarms[1])
  end
  if page == 3 then
    if timerWhy ~= '' then return timerWhy end
    return counting and 'counting down' or 'Pick the minutes and the seconds, then Start — an empty one is zero'
  end
  if chronoWhy ~= '' then return chronoWhy end
  if chronoRun then return 'counting up: Pause holds it, Reset goes back to zero' end
  if chrono_now() > 0 then return 'held: Start goes on, Reset goes back to zero' end
  return 'Start counts up from zero'
end

local function big_text()
  if fired ~= nil then return fired.when end
  if page == 3 then return mmss(left_now()) end
  if page == 4 then return chrono_text(chrono_now()) end
  return ''
end

-- ============================================================ la targeta d'un camp
--
-- **La targeta és del sistema** (0.189.102, l'amo: *«els vull com l'original, un desplegable que et mostr un modal amb els
-- minuts/hores/dies i dos botons per anar endavant i endarrere … Tot sota UIControl. No vull controls pintats i
-- artesanals»*): the field is a door and what opens is **`ui.picker`**, the card of the paper project's `OptionPicker` —
-- the name of the field, the values of one page as the rows of the system, the chosen one as a band of ink, and **the two
-- keys of the foot** that turn the page. The app says the words and which one is chosen and hears the pick: **it does not
-- lay the card out, paint it or walk it** (docs/30, docs/90).
--
-- **El que un control demana es fa al torn següent i no dins del seu ganxo**: un botó que s'ha deixat anar es torna a
-- pintar ell mateix *després* que el ganxo ha repintat la pàgina, i la seva paraula queda estampada al lloc on ja no és
-- (vist a l'emulador: el «Delete» d'una pàgina que ja no el té). És la mateixa regla que el `tz` ja segueix amb el botó
-- del rellotge: el ganxo deixa el desig i el bucle el fa (docs/30, docs/90).
local wanted = nil

-- **Les dues feines que es declaren més avall i que la targeta necessita deixades per escrit**: the pick of a value lays the
-- form out again and repaints the page, and a `wanted` that named a local of a hundred lines below would name a global that
-- does not exist —and the app dies with `attempt to call a nil value` on the very tap that chose a day— (0.189.102).
local paint_page, layout
local card, card_field = nil, nil

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

-- **El que la targeta ha de saber del camp és el nom, les paraules i la triada, i res més**: the card is as big as the room
-- under the strip of tabs and the kernel works out how many rows fit in it, which page the chosen value is on and how many
-- the two keys of the foot can turn (docs/30, docs/90).
local function open_picker(field, choose)
  close_card()
  -- **Una targeta sense valors no s'obre** (0.189.102): a card with no words is a card a person cannot answer, and the fault
  -- has to be read in the log and not looked for on the glass —a blank card is the same picture as a card nobody filled—
  -- (docs/30, docs/90).
  if type(field.items) ~= 'table' or #field.items == 0 then
    console.print('clock: the field ' .. tostring(field.label) .. ' has no values to show')
    return refused('targeta', 'the field has no values')
  end
  local top = MARGIN + TABS_H + GAP
  local picker, why = ui.picker(wid - 2 * MARGIN, hei - top - MARGIN)
  if picker == nil then
    return refused('picker', why)
  end
  picker.title = field.label
  picker.items = field.items
  picker.index = field.index
  picker.on_pick = function(self, index)
    -- **Llegir, tancar i llavors fer**: the card is gone when the field is written, so the page below it does not move with
    -- the finger still on the glass (docs/90).
    wanted = function()
      close_card()
      if index ~= nil and index > 0 then
        choose(index)
        field.index = index
        hint = ''
        save_form()
      end
      layout()
      paint_page()
    end
  end
  picker:place(MARGIN, top)
  card, card_field = picker, field
  field.choosing = true
  -- **El cercle va a la targeta**: it is the only stop of the walk while it is up —`ui.picker` is a card of the system and
  -- the page under it answers nothing— and its two keys are the ones that walk the values (docs/30, docs/90).
  ui.focus(picker)
  ui.present(kCheapMode)
end

-- ============================================================ el flux de la pàgina
--
-- **Les quatre pantalles són un sol flux vertical**, com l'arbre de PaperOS: la tira de pestanyes, la caixa que es menja
-- el lloc que sobra (l'esfera a Time, la llista a Alarm), la línia gran, la nota, els camps, la caixa de text i els
-- botons, amb el marge de 12 i el buit de 8 de l'app del paper.
local box = {}
local rows_box

-- El que ja és al vidre: el minut de l'esfera, la línia gran i la nota. Una línia que s'escriu amb el que ja diu és una
-- passada del panell pagada per res.
local tick = { face = nil, big = nil, note = nil }

local function face_box(y, h)
  box.face = placed(MARGIN, y, wid - 2 * MARGIN, h)
end

rows_box = function(y, h)
  local room = math.min(h, MAX_ROWS * ROW_H)
  box.rows = placed(MARGIN, y, wid - 2 * MARGIN, room)
end

local function big_box(y, h) box.big = placed(MARGIN, y, wid - 2 * MARGIN, h) end

local function note_box_of(y, h)
  box.note = placed(MARGIN, y, wid - 2 * MARGIN, h)
end

-- L'altura de la nota és la de les ratlles que hi entren, i es pregunta abans de col·locar-la perquè el flux ha de saber
-- quant ocupa (una nota de dues ratlles empeny els camps vuit píxels i no els deixa a sobre).
local function note_height()
  local text = note_text()
  if text == '' then return 0 end
  return math.max(1, #wrapped(text, wid - 2 * MARGIN, kSmall)) * line(kSmall)
end

local function place_pair(y, h, left, right)
  local w = (wid - 2 * MARGIN - GAP) // 2
  left:resize(w, h)
  left:place(MARGIN, y)
  right:resize(w, h)
  right:place(MARGIN + w + GAP, y)
  box.pair = placed(MARGIN, y, wid - 2 * MARGIN, h)
end

local function place_one(control, y, h)
  control:resize(wid - 2 * MARGIN, h)
  control:place(MARGIN, y)
end

layout = function()
  measure()
  box = {}
  for _, control in ipairs({ tabs, dismiss, alarmList, daySel, moSel, hourSel, quarSel, minSel, secSel,
                             noteBox, primary, secondary }) do
    if control ~= nil then control:place(0, hei + 400) end
  end
  local items = {}

  local function add(h, place, flex)
    items[#items + 1] = { h = h, place = place, flex = flex or false }
  end

  if fired ~= nil then
    -- **La pantalla gran s'ho queda tot**: l'hora, la nota i el botó que se l'emporta (PaperOS, `apply`).
    add(line(kBig), big_box)
    add(math.max(note_height(), line(kSmall)), note_box_of)
    add(kButton, function(y, h)
      place_one(dismiss, y, h)
      -- **El cercle va a l'única porta que hi ha**: sense això el dial es queda en un control que aquesta pantalla no té, i
      -- una placa sense tàctil no podria tancar l'alarma (docs/30).
      ui.focus(dismiss)
    end)
  else
    add(TABS_H, function(y, h)
      tabs:resize(wid - 2 * MARGIN, h)
      tabs:place(MARGIN, y)
      box.tabs = placed(MARGIN, y, wid - 2 * MARGIN, h)
    end)

    if page == 1 then
      add(0, face_box, true)
    elseif page == 2 then
      add(0, rows_box, true)
    else
      add(line(kBig), big_box)
    end

    local said = note_height()
    if said > 0 then add(said, note_box_of) end

    if page == 2 then
      add(kField, function(y, h) place_pair(y, h, daySel, moSel) end)
      add(kField, function(y, h) place_pair(y, h, hourSel, quarSel) end)
      add(kBox, function(y, h)
        noteBox:resize(wid - 2 * MARGIN, h)
        noteBox:place(MARGIN, y)
        box.text = placed(MARGIN, y, wid - 2 * MARGIN, h)
      end)
    elseif page == 3 then
      add(kField, function(y, h) place_pair(y, h, minSel, secSel) end)
    end

    if page ~= 1 then add(kButton, function(y, h) place_one(primary, y, h) end) end
    if (page == 2 and marked > 0) or page >= 3 then
      add(kButton, function(y, h) place_one(secondary, y, h) end)
    end
  end

  -- El lloc que sobra és del que és flexible, i el que no ho és no es mou del seu.
  local fixed = 0
  for _, it in ipairs(items) do
    if not it.flex then fixed = fixed + it.h end
  end
  local slack = hei - 2 * MARGIN - fixed - GAP * (#items - 1)
  if slack < 0 then slack = 0 end
  local y = MARGIN
  for _, it in ipairs(items) do
    local h = it.flex and slack or it.h
    it.place(y, h)
    y = y + h + GAP
  end
end

-- ============================================================ el dibuix a mà
--
-- **El que no és un control el pinta l'app**: l'esfera, la línia gran i la nota. Tot el que és un botó, un camp, una
-- caixa o la tira de pestanyes el dibuixa el nucli (docs/30, docs/90).
local function paint_note()
  local b = box.note
  if b == nil then return end
  ui.rect(b.x, b.y, b.w, b.h, 15)
  local text = note_text()
  if text == '' then return end
  local lines = wrapped(text, b.w, kSmall)
  for i, one in ipairs(lines) do
    ui.text(b.x, b.y + (i - 1) * line(kSmall), one, kSmall, INK)
  end
end

local function paint_big()
  local b = box.big
  if b == nil then return end
  ui.rect(b.x, b.y, b.w, b.h, 15)
  local text = big_text()
  if text == '' then return end
  ui.text(b.x + (b.w - ui.text_width(text, kBig)) // 2, b.y, text, kBig, INK)
end

-- El minut que l'esfera ensenya, que és el que decideix si s'ha de tornar a pintar.
local function face_key()
  local now = time.date()
  if now == nil then return 'no clock' end
  return string.format('%04d-%02d-%02d %02d:%02d', now.year, now.month, now.day, now.hour, now.min)
end

local function paint_dial()
  local b = box.face
  if b == nil then return end
  ui.rect(b.x, b.y, b.w, b.h, 15)
  paint_face(b.x, b.y, b.w, b.h, time.date())
end

local function everything()
  return { tabs, dismiss, alarmList, daySel, moSel, hourSel, quarSel, minSel, secSel, noteBox,
           primary, secondary }
end

local apply_page

-- **La pàgina sencera**, que és el que es fa en obrir i en canviar de pestanya: el paper, el dibuix a mà i els controls,
-- que s'han de tornar a demanar perquè `ui.clear` ha pintat el paper a sobre del que hi tenien (docs/30).
paint_page = function(held)
  if apply_page ~= nil then apply_page() end
  -- **El rentat de la pàgina va amb la seva qualitat i no amb la de per defecte**: sense un mode, `ui.clear` només
  -- esborra el que el sistema ja té marcat com a brut, i el dibuix d'un control que ha marxat es queda al llenç del pare
  -- per sempre —la pantalla de l'alarma tornava amb els botons de la pàgina d'abans a sota—. Amb el mode, el rentat és
  -- de tota la vista i d'una passada neta (docs/30, docs/50).
  ui.clear(15, kCleanMode)
  paint_dial()
  paint_big()
  paint_note()
  for _, control in ipairs(everything()) do
    if control ~= nil then control:invalidate() end
  end
  -- **El que acaba d'arribar al vidre no es torna a demanar**: la pàgina sencera ja porta l'esfera, la línia gran i la
  -- nota que diu aquest minut (docs/50).
  tick.face, tick.big, tick.note = face_key(), big_text(), note_text()
  -- **I la passada que marxa de l'esfera ha de ser neta** (PaperOS, `Clock.md`): la cara porta un gris, la passada barata
  -- només en té dos de nivells i el que queda de l'anell a la pestanya de les alarmes és una taca que no marxa mai. Costa
  -- el flaix d'un `gc16` i es paga un cop per canvi de pestanya, que és el que l'app del paper va triar.
  if not held then ui.present(kCleanMode) end
end

-- ============================================================ el que fa cada pantalla

-- El que el formulari té a dins: la fila que s'està desant (zero = una de nova) i la promesa del compte enrere.
local armedTimer = 0
local editing = 0

-- **El febrer del traspàs és el que diu el sistema** (`time.leap`), i el dia 31 de febrer no s'amaga de la llista: es
-- refusa amb una frase, perquè una llista que canvia mentre un dit la fa servir és un control que es mou sota la mà.
local function days_in_month(month)
  local days = { 31, (time.leap() and 29 or 28), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
  return days[month] or 31
end

local function clear_form()
  daySel.index = 0
  moSel.index = 0
  hourSel.index = 0
  quarSel.index = 0
  noteBox.text = ''
end

local function rows_dirty()
  if alarmList ~= nil then
    alarmList:release()
    alarmList = nil
  end
end

local arm_countdown

local function dismiss_alarm()
  fired = nil
  layout()
  paint_page()
end

-- **Afegir o desar**: un dia i un mes triats són una data i no ho són si falta l'altre, i el 31 de febrer es refusa amb
-- una frase en lloc d'amagar el dia de la llista (PaperOS, `primary.on_click`).
local function add_or_save()
  local month, day = moSel.index, daySel.index
  local hour, quar = hourSel.index, quarSel.index
  local now = time.date()

  if month == 0 or day == 0 then month, day = nil, nil end

  if (month and not day) or (day and not month) then
    hint = 'Pick the day and the month, or leave both empty'
  elseif hour == 0 or quar == 0 then
    hint = 'Pick the hour and the quarter (00, 15, 30 or 45)'
  elseif day ~= nil and now ~= nil and day > days_in_month(month) then
    hint = 'That month has no such day'
  else
    local one = { h = hour - 1, m = (quar - 1) * 15, note = noteBox.text or '' }
    if day ~= nil then
      one.d, one.mo = day, month
    end
    if editing ~= 0 then
      alarms[editing] = one
    else
      alarms[#alarms + 1] = one
    end
    editing = 0
    clear_form()
    hint = ''
    sort_alarms()
    write_alarms()
    rows_dirty()
    arm_next()
  end

  layout()
  paint_page()
end

local function delete_alarm()
  if marked > 0 and alarms[marked] ~= nil then
    table.remove(alarms, marked)
    marked = 0
    clear_form()
    editing = 0
    hint = ''
    sort_alarms()
    write_alarms()
    rows_dirty()
    arm_next()
  else
    hint = 'Tap a row first, then Delete takes it away'
  end
  layout()
  paint_page()
end

-- **El tap d'una fila porta la seva alarma al formulari**, i no és un segon Delete: la fila es queda marcada i la
-- paraula segueix dient Delete (decisió de l'amo a PaperOS, 4/10).
local function load_form(i)
  local one = alarms[i]
  if one == nil then return end
  daySel.index = one.d or 0
  moSel.index = one.mo or 0
  hourSel.index = one.h + 1
  local q = math.floor((one.m or 0) / 15)
  quarSel.index = q + 1
  noteBox.text = one.note or ''
  editing = i
  hint = ((one.m or 0) % 15) ~= 0
    and string.format('%02d:%02d is not a quarter: it is saved as %02d:%02d', one.h, one.m, one.h, q * 15)
    or ''
end

local function start_or_pause()
  if page == 3 then
    if counting then
      leftMs = left_now()
      counting = false
      armedTimer = 0
      leftAt = 0
      arm_next()
    else
      -- **Un camp buit és un zero** (l'amo: *«cal posar el segons!!! ridicul! si no els poso, s'ha d'assumir 00»*): a countdown
      -- of five minutes is five minutes and no seconds, and a page that demands both fields for it is a page that asks a
      -- question whose answer is already known. **El que sí que no es pot** és un compte enrere de res (docs/30, docs/45).
      local minutes = (minSel.index > 0) and (minSel.index - 1) or 0
      local seconds = (secSel.index > 0) and (secSel.index - 1) or 0
      local ms = (minutes * 60 + seconds) * 1000
      if ms < 1000 then
        hint = 'A countdown of nothing is not one: pick the minutes or the seconds'
        layout()
        paint_page()
        return
      end
      leftMs = ms
      leftFrom = sys.time_ms()
      leftAt = epoch_ms()
      counting = true
      hint = ''
      timerWhy = ''
      arm_countdown()
    end
  elseif page == 4 then
    if chronoRun then
      chronoAcc = chrono_now()
      chronoRun = false
      chronoAt = 0
    else
      chronoFrom = sys.time_ms()
      chronoAt = epoch_ms()
      chronoRun = true
    end
    chronoWhy = ''
    hint = ''
  end
  -- **Cada canvi del que corre va al disc abans de pintar-lo**: una placa que es queda sense bateria a mig minut d'haver
  -- arrencat el cronòmetre és un cronòmetre que ha de tornar (docs/30).
  write_run()
  layout()
  paint_page()
end

local function reset_or_hold()
  if page == 2 then
    delete_alarm()
    return
  end
  if page == 4 then
    chronoRun = false
    chronoAcc = 0
    chronoFrom = 0
    chronoAt = 0
    chronoWhy = ''
  else
    counting = false
    leftMs = 0
    leftAt = 0
    timerWhy = ''
    armedTimer = 0
    arm_next()
  end
  hint = ''
  write_run()
  layout()
  paint_page()
end

-- ============================================================ la llista de les alarmes
-- **La llista de les alarmes són files del sistema amb el seu toc**: quatre com a molt —el que en diu la frase de sota— i
-- cada fila porta l'hora i la nota, que és el dibuix de la llista de PaperOS.
rows_box = function(y, h)
  local room = math.min(h, MAX_ROWS * ROW_H)
  box.rows = placed(MARGIN, y, wid - 2 * MARGIN, room)
  if alarmList ~= nil and box.rows_w == box.rows.w and box.rows_h == room then
    alarmList:place(MARGIN, y)
    return
  end
  if alarmList ~= nil then alarmList:release() end
  alarmList = nil
  local list, why = ui.list(box.rows.w, room)
  if list == nil then return refused('the list of alarms', why) end
  list.row_height = ROW_H
  list.separator = true
  for i = 1, math.min(#alarms, MAX_ROWS) do
    local entry = ui.list_entry(box.rows.w, ROW_H)
    if entry ~= nil then
      local when_text, note = row_text(alarms[i])
      -- **Les paraules no tapen la línia del separador**: un `ui.label` pinta el seu paper i un de l'altura de la fila
      -- s'emporta la ratlla de sota, que és la que fa que dues alarmes es llegeixin com dues files (docs/30).
      local when_words = ui.label(box.rows.w // 2, ROW_H - 2)
      local note_words = ui.label(box.rows.w // 2 - 24, ROW_H - 2)
      if when_words ~= nil and note_words ~= nil then
        when_words.text = when_text
        when_words.valign = 'middle'
        when_words.scale = kSmall
        when_words:place(12, 0)
        note_words.text = note
        note_words.valign = 'middle'
        note_words.scale = kSmall
        note_words:place(12 + ui.text_width(when_text, kSmall) + 16, 0)
        entry:add(when_words)
        entry:add(note_words)
      end
      entry.marked = (marked == i)
      entry.on_click = function()
        marked = i
        load_form(i)
        save_form()
        rows_box(y, h)
        layout()
        paint_page()
      end
      list:add(entry)
    end
  end
  list.marked = marked
  list:place(MARGIN, y)
  alarmList = list
  box.rows_w, box.rows_h = box.rows.w, room
end

-- ============================================================ els controls, una vegada
local MONTHS = { 'January', 'February', 'March', 'April', 'May', 'June',
                 'July', 'August', 'September', 'October', 'November', 'December' }

local named = {}      -- el nom del bloc `state` i el control que el porta

local function field(name, label, items, choose)
  local one
  local why
  one, why = ui.selector(2, 2)
  if one == nil then
    refused(name, why)
    return nil
  end
  one.label = label
  one.items = items
  one.empty = '-'
  one.index = 0
  one.scale = kBody
  one.on_open = function(self)
    wanted = function() open_picker(self, choose) end
  end
  one.on_focus = function(self, on) remember(on, name) end
  named[name] = one
  return one
end

local function make_controls()
  local why
  tabs, why = ui.tabs(2, 2)
  if tabs == nil then return refused('the strip of tabs', why) end
  tabs.items = TABS
  tabs.index = page
  -- **Les quatre paraules hi han de cabre senceres**: a quatre columnes de 129 píxels, «Chrono» a la lletra del cos es
  -- queda en «Chr…», i el paper les escriu a la petita (docs/92).
  tabs.scale = kSmall
  tabs.on_change = function(self, index)
    if index == nil or index == page then return end
    wanted = function()
      close_card()
      page = index
      hint = ''
      layout()
      paint_page()
      save_form()
    end
  end
  named.tabs = tabs

  daySel = field('day', 'Day', DAYS, function(i) end)
  moSel = field('month', 'Month', MONTHS, function(i) end)
  hourSel = field('hour', 'Hour', HOURS, function(i) end)
  quarSel = field('minute', 'Minute', QUARTERS, function(i) end)
  minSel = field('minutes', 'Minutes', MINUTES, function(i) end)
  secSel = field('seconds', 'Seconds', SECONDS, function(i) end)

  noteBox, why = ui.textbox(2, 2)
  if noteBox == nil then return refused('the note', why) end
  noteBox.text = ''
  noteBox.scale = kBody
  noteBox.keyboard = 'alpha'
  noteBox.on_focus = function(self, on) remember(on, 'note') end
  named.note = noteBox

  primary, why = ui.button(2, 2)
  if primary == nil then return refused('the button', why) end
  primary.title = 'Add'
  primary.primary = true
  primary.scale = kBody
  primary.on_click = function()
    wanted = function()
      if page == 2 then add_or_save() else start_or_pause() end
    end
  end
  primary.on_focus = function(self, on) remember(on, 'commit') end
  named.commit = primary

  secondary, why = ui.button(2, 2)
  if secondary == nil then return refused('the second button', why) end
  secondary.title = 'Reset'
  secondary.scale = kBody
  secondary.on_click = function() wanted = function() reset_or_hold() end end
  secondary.on_focus = function(self, on) remember(on, 'more') end
  named.more = secondary

  dismiss, why = ui.button(2, 2)
  if dismiss == nil then return refused('the door of the alarm', why) end
  dismiss.title = 'Dismiss'
  dismiss.primary = true
  dismiss.scale = kBody
  dismiss.on_click = function() wanted = function() dismiss_alarm() end end
  named.dismiss = dismiss
end

-- **El nom del botó és el treball de la pestanya que hi és al davant**, i el bategador de la pestanya no és cap d'ells:
-- la pantalla que només s'ha de llegir no demana el dit (PaperOS, `apply`).
apply_page = function()
  tabs.index = page
  if page == 2 then
    primary.title = (editing ~= 0) and 'Save' or 'Add'
    secondary.title = 'Delete'
  elseif page == 4 then
    primary.title = chronoRun and 'Pause' or 'Start'
    secondary.title = 'Reset'
  else
    primary.title = counting and 'Pause' or 'Start'
    secondary.title = 'Reset'
  end
end

-- ============================================================ el que sona
arm_countdown = function()
  if armedTimer ~= 0 then
    alarm.cancel(armedTimer)
    armedTimer = 0
  end
  local left = left_now()
  if left <= 0 then return end
  -- **`persist` perquè el compte enrere no és d'aquesta arrencada**: the countdown has to ring even if the board was
  -- unplugged and plugged back in, which is the same promise the daily alarm asks for with its `repeat` (docs/30).
  local ok, id = pcall(alarm.at, { in_ms = left, raise = true, payload = 'timer', persist = true })
  armedTimer = (ok and type(id) == 'number') and id or 0
  if armedTimer == 0 then
    console.print('clock: the countdown is running without a promise (' .. tostring(ok and id or id.code or id) .. ')')
  end
end

-- **El compte enrere és una promesa com l'alarma i desvetlla la placa igual**, o sigui que quan venç el sistema obre
-- l'app, crida això i la pantalla gran és la mateixa per als dos (PaperOS, `countdownDone`).
function on_alarm(id, payload, meta)
  if armedTimer ~= 0 and id == armedTimer then
    armedTimer = 0
    counting = false
    leftMs = 0
    leftAt = 0
    timerWhy = ''
    fired = { when = 'Time is up', note = 'The countdown has finished.' }
    out_of_place = true
    write_run()
    return
  end
  if armed ~= 0 and id == armed then
    armed = 0
    local when_text = tostring(payload or '')
    local note = 'It is time.'
    for _, one in ipairs(alarms) do
      local text, said = row_text(one)
      if text == when_text then
        note = (said ~= '') and said or 'It is time.'
        break
      end
    end
    fired = { when = (when_text ~= '') and when_text or 'Alarm', note = note }
    out_of_place = true
    return
  end
  -- **I el que ve d'un `raise` no té cap id amb qui comparar-se** (0.190.7): a page that was raised is a fresh start, o sigui
  -- que `armed` i `armedTimer` són zero i l'única cosa que diu què ha sonat és el payload —el text de la fila tal com es va
  -- armar, o `timer`—. Sense això un despertador que sona amb el rellotge tancat obre el rellotge i no diu que ha sonat, que és
  -- el que passava des que el `raise` existeix (docs/30, docs/60).
  if fired == nil and type(payload) == 'string' and payload ~= '' then
    if payload == 'timer' then
      fired = { when = 'Time is up', note = 'The countdown has finished.' }
    else
      local note = 'It is time.'
      for _, one in ipairs(alarms) do
        local text, said = row_text(one)
        if text == payload then
          note = (said ~= '') and said or 'It is time.'
          break
        end
      end
      fired = { when = payload, note = note }
    end
    out_of_place = true
  end
end

-- **Una finestra només porta la crida de l'alarma**: el sistema carrega l'entry per obrir l'app i tot seguit crida
-- `on_alarm`, i sense aquesta porta cada finestra armaria una alarma nova i el sistema es cridaria a si mateix per sempre
-- (docs/30, provat a la placa).
if not console.is_foreground() then return end

-- ============================================================ l'app
read_alarms()
sort_alarms()
-- **El cronòmetre i el compte enrere tornen abans que la pàgina** (0.189.103): el que el disc diu és el que la persona va
-- deixar corrent, i el compte enrere torna a deixar la seva promesa al sistema perquè soni encara que l'app no hi sigui.
read_run()

make_controls()

-- **I el compte enrere que la persona havia triat, ara que els dos camps existeixen**: a countdown that comes back with the
-- right minutes and seconds is a countdown that was never lost (docs/30, docs/45).
if runPick ~= nil and runPick.min ~= nil and runPick.sec ~= nil then
  minSel.index = math.min(math.max(1, runPick.min + 1), #MINUTES)
  secSel.index = math.min(math.max(1, runPick.sec + 1), #SECONDS)
end

-- **La vista que la persona va deixar**: la pestanya, la fila marcada i la nota, i el cercle al control que el tenia. El
-- que no sigui d'aquesta forma es llença i la pàgina arrenca com una app nova (docs/30).
do
  local was = load_state()
  if was ~= nil then
    local tab = tonumber(was.tab or '')
    if tab ~= nil then page = math.min(math.max(1, tab), #TABS) end
    local mark = tonumber(was.marked or '')
    if mark ~= nil then marked = math.min(math.max(0, mark), #alarms) end
    if was.text.note ~= nil then
      noteBox.text = was.text.note
    end
    shape.focus = was.focus
  end
end

-- **Les promeses velles d'aquesta app es donen per acabades abans d'armar-ne una de nova**: la cua del sistema sobreviu
-- als reinicis i a les còpies de la placa, i una app que n'arriba amb sis de les seves no en pot armar cap més
-- (`E_NOMEM`, mesurat a l'emulador). El que compta és una promesa i no dues (docs/30, docs/60).
-- **I només les seves**: fins a la 0.189.172 aquest bucle buidava la cua sencera i obrir el `Clock` esborrava els avisos de
-- l'`agenda` i del `meteo`, que els tornaven a armar a la càrrega següent (docs/30, docs/60).
local function forget_old_promises()
  local ok, mine = pcall(alarm.list)
  if not ok or type(mine) ~= 'table' then return end
  for _, one in ipairs(mine) do
    if type(one) == 'table' and one.id ~= nil and one.app == app.id then pcall(alarm.cancel, one.id) end
  end
end

forget_old_promises()
-- **I la promesa del compte enrere es torna a deixar DESPRÉS d'esborrar les velles** (0.189.103): `forget_old_promises` buida
-- la cua d'aquesta app —una alarma de la còpia anterior de la placa no pot comptar com una d'aquesta— i armar-la abans és
-- armar-la perquè la tiri la línia següent (docs/30, docs/60).
if counting then arm_countdown() end
-- I el que corre tal com és ara al disc: el moment de la tornada és l'ancora nova (docs/30).
write_run()
apply_page()
layout()
paint_page()
arm_next()

if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end

-- **Una alarma és del sistema i la seva crida és entre dos torns**: el que ha sonat es deixa dit i el bucle fa la feina.
-- **I no s'esborra el que el ganxo pot haver deixat** (0.190.7): una pàgina pujada per una alarma rep el ganxo just després
-- d'aquestes línies i, si aquí es posés la bandera a fals, la pantalla de l'alarma no es pintaria mai. Es neteja només quan no
-- hi ha res a ensenyar.
if fired == nil then out_of_place = false end

while true do
  -- **El desig d'un control**, que és el que ha demanat el dit o el dial al torn anterior.
  if wanted ~= nil then
    local doing = wanted
    wanted = nil
    -- **Mitja passa abans de fer-ho**: el sistema acaba d'escriure el botó que s'ha deixat anar *després* del ganxo, i
    -- una pàgina repintada dins del mateix gest es queda amb la paraula del botó vell estampada al lloc on ja no és.
    sys.delay(300):await()
    doing()
  end

  if out_of_place then
    out_of_place = false
    apply_page()
    layout()
    paint_page()
    -- **La pantalla de l'alarma es pinta dues vegades, i la segona és la que compta**: el sistema acaba d'escriure-hi
    -- el deixar anar del botó que ha disparat el compte enrere, i aquella passada cau a sobre de la pàgina nova i hi
    -- deixa el dibuix de la pàgina d'abans. La segona passada el renta, i es paga un cop per alarma.
    sys.delay(400):await()
    layout()
    paint_page()
  end

  local caps = ui.caps()
  if caps.width ~= wid or caps.height ~= hei then
    apply_page()
    layout()
    paint_page()
  end

  local dirty = false

  -- **El minut de l'esfera**: només el dibuix de l'esfera, no la pàgina, perquè el que ha canviat són les busques.
  local face_dirty = false
  if fired == nil and page == 1 then
    local key = face_key()
    if key ~= tick.face then
      tick.face = key
      paint_dial()
      face_dirty = true
    end
  end

  local big_now = big_text()
  if big_now ~= tick.big then
    tick.big = big_now
    paint_big()
    dirty = true
  end

  local note_now = note_text()
  if note_now ~= tick.note then
    tick.note = note_now
    paint_note()
    dirty = true
  end

  -- **La qualitat la tria el sistema, i és una decisió**: la cara porta un gris que el `du` no pot pintar (PaperOS,
  -- `Clock.md`) i el número del compte enrere canvia cada segon, on el `du` deixa cinc setzens de la xifra anterior i el
  -- número s'acaba llegint doble. El sistema és qui sap què aguanta el vidre: `gld16` mentre va bé i el rentat net quan
  -- diu que ja en té prou (docs/30, docs/50).
  if dirty or face_dirty then ui.present(kSoftMode) end

  -- **I un toc fora de la targeta la tanca**, que és el que vol dir una targeta sobre una pàgina: the values and the two keys
  -- are the system's and the page under it answers nothing, so the way out is a finger that lands beside the card. It is asked
  -- of the **gestures** and not of `touch.read()`: a card of the system owns the fingers that fall on it, and a page that read
  -- the raw points while one is up read none of them —nor the ones beside it— (docs/30, docs/90).
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
