-- agenda: **un calendari perpetu amb events** (0.189.171, portada de `data/app/Agenda` del projecte del paper).
--
-- **LA INTERFÍCIE ÉS LA DE L'ORIGINAL** (l'amo: *«Porta l'agenda, recorda... tot EXACTALENT IGUAL, però amb els nostres
-- controls UI i les nostres crides a sistema»*): el MES en una pantalla i EL DIA del mes en una altra.
--
--   EL MES      el calendari del sistema (`ui.month`) amb els dies que tenen alguna cosa **marcats amb un punt** i avui amb
--               el seu marc, i el mes i l'any escrits a sobre. Un dit tria un dia i el dia s'obre; un dit que camina de
--               costat gira el MES i un que camina de dalt a baix mou l'ANY, perquè un any són dotze voltes d'un mes i
--               ningú no les ha de comptar. **El control del sistema camina els dies amb les fletxes i tria amb el centre**
--               (0.75.0); el gest del costat el llegeix aquesta pàgina, perquè un gest no arriba mai a l'arbre dels controls
--               i el que no es pren aquí és un gest que no fa res (docs/30).
--
--   EL DIA      la data escrita, els events D'AQUELL DIA en una llista —l'hora, `all day` o l'interval, i què és, en l'ordre
--               del rellotge i el dia sense hora primer—, i quatre botons: New, Change, Delete i Back. La fila que un dit
--               marca és sobre la qual actuen Change i Delete, i la línia de sota de la llista diu què hi ha: la seva nota i
--               si es repeteix.
--
--   EL FORMULARI un nom, una nota, la data, l'hora i el minut, quant dura i cada quant torna, més `All day` pel dia que no
--               té hora. **Les dues caixes són l'únic que s'escriu** i tot lo altre s'escull (`ui.selector` amb el
--               desplegable del sistema, `ui.picker`), o sigui que gairebé no hi ha res a validar: una hora de 24 no
--               existeix perquè la llista s'acaba a 23. **I la data es tria al calendari**: `Date` porta al mes amb el dia
--               del formulari triat, i un dit sobre un dia torna al formulari.
--
-- **ON VIUEN ELS EVENTS, QUE ÉS LA DECISIÓ DE L'AMO DEL 7**: al terreny de l'app (`fs.data`), que és el que el sistema tria
-- —la targeta si hi és i el flaix si no—, i el fitxer és text, una línia per event (docs/30, docs/70). **I on és es diu a la
-- pantalla**, perquè és la diferència entre un event que sobreviu a la pròxima càrrega del firmware i un que no.
--
-- **I L'AVÍS, QUE ÉS L'ALTRA DECISIÓ DE L'AMO DEL 7**: una promesa es guarda amb l'aparell encès o adormit i **no es guarda
-- amb l'aparell apagat** (la regla del 3 d'octubre: apagar no arma res). O sigui que es promet **el pròxim event, exactament
-- un** —`alarm.at`— i el que queda massa lluny per a una sola promesa es promet **per etapes**, com fa el `Clock`: un
-- enllaç arriba tan lluny com arriba una promesa (`alarm.caps().max`) i, quan cau, el ganxo arma el següent.
-- **I el que sona es fa dins del ganxo** (la lliçó de la 0.189.145): una promesa crida `on_alarm` i l'app pot ser una
-- finestra, o sigui que el ganxo arma el següent avís i deixa dit quin event ha sonat; el torn de la pàgina obre el dia.
-- **L'original obria una targeta d'avís del sistema i aquí no hi ha cap canal** (`ui.notification_bar` és un control que la
-- pàgina pinta i no un avís del nucli, docs/30): el que fa l'avís és **obrir la pàgina al dia de l'event**, que és el que la
-- targeta feia en tocar-se (docs/30, docs/45).

local caps = sys.caps()
if not caps.display then
  console.print('agenda: this board has no glass')
  return
end

-- ============================================================== the sizes of the paper project
--
-- Es demanen en píxels i no en passos perquè les dues cares d'aquest sistema no fan la mateixa línia: el projecte del paper
-- tenia `small` 33, `body` 43 i `title` 57 (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 8 then scale = 8 end

  return scale
end

local kSmall = scale_for(33)
local kBody = scale_for(43)
local kTitle = scale_for(57)

local function line(scale)
  return scale * ui.line_height(1)
end

local INK, PAPER = 0, 15
local MARGIN, GAP = 20, 10

-- **Els noms dels mesos i dels dies són de l'app** i no del nucli (una llengua no és del nucli, docs/30): els del vidre
-- d'aquest sistema són els anglesos. El calendari del sistema en vol set de curts **de dilluns a diumenge** i gira la primera
-- columna amb `time.week()` (docs/30), i el text de les dates vol diumenge al zero, que és com compta el rellotge.
local MONTHS = { 'January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October',
                 'November', 'December' }
local DAY_NAMES = { 'Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday' }
local DAY_SHORT = { 'Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat' }
local MONTH_SHORT = { 'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec' }
local WEEKDAYS = { 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su' }
local REPEAT_NAMES = { 'once', 'every day', 'every week', 'every month', 'every quarter', 'every year' }

-- **Quants events cap en una pantalla de dia**: és el número del projecte del paper i no un gust —el que no hi cap es COMPTA
-- i es diu a la línia de sota— (docs/45).
local MaxRows = 8

-- **La banda del teclat fa quatre files de seixanta-quatre píxels** (256, docs/30), i `kb:height()` no ho diu fins que la
-- banda és al vidre: el formulari s'ha de fer per a la vista que quedarà **abans** que el teclat hi sigui, o la primera
-- caixa que s'obre fa desaparèixer la meitat de les files. El número es mesura la primera vegada que la vista s'encongeix i
-- es queda per a la resta de l'encesa.
local kBandHeight = 256
local band_h = 0

-- The years the swipe may reach. 2000 is where this app's calendar counts from and 2059 keeps the span inside what one count
-- of a month can carry; what falls outside is refused instead of invented, and the swipe stops at the edge.
local FirstYear, LastYear = 2000, 2059

-- The options of the fields that are picked: the hours, the minutes, the lengths and the repeats. The day is not one of them
-- —it is the one a finger touches on the calendar—.
local HOURS, MINUTES = {}, {}
for i = 0, 23 do HOURS[i + 1] = string.format('%02d', i) end
for i = 0, 59 do MINUTES[i + 1] = string.format('%02d', i) end

-- **La durada és un sol camp i no dues hores** (the owner, 26/10: *«duracions de fins a 9h:30»*): the end is the start plus
-- this, and zero is «no end», which is an event that is a moment. The midnight rule is what the day can hold, and a start
-- late enough that `start + this` would cross midnight is refused and said.
local LENGTHS = { 0, 15, 30, 45, 60, 90, 120, 180, 240, 480, 570 }
local LENGTH_NAMES = { 'no end', '15 min', '30 min', '45 min', '1 hour', '1h 30', '2 hours', '3 hours', '4 hours',
                       '8 hours', '9h 30' }

-- ============================================================== the module and the file

-- **El mòdul de les dades és un fitxer de l'app** (`require` llegeix la carpeta de l'app, docs/30) i no sap res de la pantalla.
local agenda = nil
do
  local ok, module = pcall(require, 'events')
  -- **Un mòdul que no hi és és mitja app, i mitja app és pitjor que una que diu per què**: sense això la primera crida que
  -- necessita el calendari petaria amb un «attempt to index a nil value» que no diu res del fitxer que falta.
  if not ok or type(module) ~= 'table' then
    console.print('agenda: this app needs its own module: events.lua (' .. tostring(module) .. ')')
    return
  end
  agenda = module
end

local file_ok, agenda_file = pcall(fs.data, agenda.File)
if not file_ok or type(agenda_file) ~= 'string' or agenda_file == '' then
  console.print('agenda: there is no ground for the file of the events')
  return
end

-- ============================================================== what the page is (docs/30 § la sessió `state`)

local kStateBytes = 1024
local kLayout = 'v1'   -- bumped when a control is renamed or the page changes shape

local state = nil      -- the block: asked once and kept for the life of the app

local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end
  return state
end

local function escape(text)
  return (tostring(text or ''):gsub('\\', '\\\\'):gsub('\n', '\\n'))
end

local function unescape(text)
  return (text:gsub('\\(.)', function(c) return (c == 'n') and '\n' or c end))
end

-- **El codi que desa i recupera és el mateix de totes les apps**, amb les claus d'aquesta afegides a la mateixa llista
-- (docs/30): the five of the format and the ones this page has —which screen, which day, which month the calendar was
-- showing, which event the form is writing and what the fields hold—, perquè tornar a una pàgina amb el formulari buit és
-- perdre el que algú estava escrivint. És el mateix format d'una cosa per línia que l'historial de la calculadora.
local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'focus', 'page', 'tab', 'scroll', 'marked', 'day', 'y', 'mo', 'editing', 'hour', 'min',
                         'len', 'rep', 'formy', 'formmo', 'formd' }) do
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
  for one in body:gmatch('[^\n]+') do
    if first then
      first = false
      if one ~= kLayout then return nil end
    elseif one:sub(1, 6) == 'check:' then
      local name, value = one:match('^check:([^=]+)=(%d)$')
      if name ~= nil then now.check[name] = (value == '1') end
    elseif one:sub(1, 5) == 'text:' then
      local name, value = one:match('^text:([^=]+)=(.*)$')
      if name ~= nil then now.text[name] = unescape(value) end
    else
      local key, value = one:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end
  if first then return nil end
  return now
end

-- ============================================================== the state of the page

local page_ = 1        -- 1 the month, 2 the day, 3 the form
local day_ = 0         -- the day of the day screen
local seenY_, seenMo_ = 0, 0
local marked_ = -1     -- the row a finger marked, zero being the first one
local editing_ = 0     -- the line of the event the form is changing
local hint_ = ''       -- what the last attempt said
local title_, note_ = '', ''
local hour_pick_, min_pick_, len_pick_, rep_pick_ = 0, 1, 1, 1
local all_day_ = false
local lenTouched_, editEnd_ = false, nil
local formY_, formMo_, formD_ = 0, 0, 0
local picking_ = false -- true while the month is the date picker of the form

-- read from the file and from the clock at every mount
local events_ = {}
local bad_ = 0
local problem_ = ''
local armed_ = 0
local armedAt_ = nil    -- the occurrence the alarm will ring at, `{ y, mo, d }`, and not the event's first day
local why_ = ''
local remindAt_ = nil   -- the occurrence that rang, while the turn that opens it has not come
local dayRows_ = {}

-- The controls of the screen that is on the glass, so `paint()` can say their words again.
local wid, hei = 0, 0
local screen = nil
local monthTitle, remindLine, monthHint1, monthHint2, cal = nil, nil, nil, nil, nil
local dayTitle, dayHint1, dayHint2, dayList = nil, nil, nil, nil
local newBtn, changeBtn, deleteBtn, backBtn = nil, nil, nil, nil
local titleBox, noteBox, allDayChk, dateBtn, saveBtn, cancelBtn = nil, nil, nil, nil, nil, nil
local hourSel, minSel, lenSel, repSel = nil, nil, nil, nil
local rowEntries_ = {}   -- the entries of the day's list, to move the mark without building the page again
local card = nil         -- the drop-down card of a field, while it is on the page
local card_field = nil   -- the field that opened it, which is the one `choosing` has to be taken off
-- **El que un control ha demanat i el fil encara no ha fet**: a card opened inside the tap that asked for it takes the same
-- tap, and the system finishes writing the field *after* the hook —so a card put up there is painted over by it—. The wish
-- is left here and the loop does it a turn later (docs/30, docs/45).
local wanted_ = nil

-- forward declarations: the screens call each other and the hooks are called by the system
local paint, open_month, open_day, open_form, save_form, shape_state

-- ============================================================== small words

-- **Un text de dues línies, tallat on cap** (the original's `wrap = true`, that a `ui.label` of this system does not have):
-- the break is the last word that fits and what does not fit in two lines is the label's business —it ends it with dots—.
local function two_lines(text, width, scale)
  if ui.text_width(text, scale) <= width then return text, '' end

  local cut = nil
  local at = 1
  while true do
    local space = text:find(' ', at, true)
    if space == nil then break end
    if ui.text_width(text:sub(1, space - 1), scale) > width then break end
    cut = space
    at = space + 1
  end
  if cut == nil then return text, '' end
  return text:sub(1, cut - 1), text:sub(cut + 1)
end

--- **Una data que cap al vidre**: the long names are the ones of the original and a glass that cannot hold them gets the
-- short ones instead of a sentence that ends in three dots, perquè una data tallada a la meitat és pitjor que una data curta
-- (docs/45).
local function date_text(w, mo, d, scale, with_year)
  local index = (agenda.weekdayOf(w, mo, d) or 0) + 1
  local text = string.format('%s %d %s %04d', DAY_NAMES[index] or '?', d, MONTHS[mo] or '?', w)
  if with_year == false then text = string.format('%s %d %s', DAY_NAMES[index] or '?', d, MONTHS[mo] or '?') end
  if ui.text_width(text, scale) <= wid - 2 * MARGIN then return text end

  local short = string.format('%s %d %s', DAY_SHORT[index] or '?', d, MONTH_SHORT[mo] or '?')
  if with_year ~= false then short = short .. string.format(' %04d', w) end
  return short
end

local function make_label(width, height, text, scale, align)
  local one = ui.label(width, height)
  if one == nil then return nil end
  one.text = text
  one.scale = scale
  one.align = align or 'left'
  one.color = INK
  one.background = PAPER
  return one
end

--- Today's day, if the month on the glass is the month of today.
local function today_in(y, mo)
  local now = time.date()
  if now == nil or now.year == nil or now.day == nil then return 0 end
  if now.year ~= y or now.month ~= mo then return 0 end
  return now.day
end

--- **On és el fitxer i quant hi ha**, que és la línia de sota del calendari quan no hi ha res més a dir.
--
-- El volum el diu el sistema (`fs.mounts()`, la targeta o el flaix) i no l'app: aquí l'app no tria on escriu —el seu terreny
-- és seu i el sistema decideix el volum— i el que ha de poder dir és on és (docs/30, docs/70).
local function where_text()
  local volume = 'flash'
  local ok, mounts = pcall(fs.mounts)
  if ok and type(mounts) == 'table' then
    local best = 0
    for _, one in ipairs(mounts) do
      if type(one) == 'table' and type(one.path) == 'string' and #one.path > best and
         agenda_file:sub(1, #one.path) == one.path then
        best = #one.path
        volume = (one.type == 'sd') and 'card' or 'flash'
      end
    end
  end

  local where = 'on the ' .. volume
  local n = #events_
  local text
  if n == 0 then
    text = where .. ', no events yet'
  elseif n == 1 then
    text = where .. ', 1 event'
  else
    text = string.format('%s, %d events', where, n)
  end
  if bad_ > 0 then text = text .. string.format(' · %d unreadable', bad_) end
  return text
end

--- The line above the calendar: which month it shows, and whether the finger is picking a day for the form.
local function month_text()
  if seenY_ == 0 then return 'the clock is not set' end

  local text = string.format('%s %04d', MONTHS[seenMo_] or '?', seenY_)
  if picking_ then text = 'Pick the day · ' .. text end
  return text
end

--- The line under the month: what the reminder is holding, or why nothing is.
--
-- **La veritat de l'avís és part de la pantalla** i no un detall: una promesa viu amb l'aparell encès o adormit i no amb
-- l'aparell apagat, i una app que callés això prometria una cosa que no pot guardar.
local function remind_text()
  if armed_ > 0 then
    local e = events_[armed_]
    -- **I la data que es diu és la de l'ocurrència i no la de la primera vegada**: un event que es repeteix cada setmana sona
    -- el dia que toca, i la línia ha de dir aquest dia (0.190.7, docs/30).
    local at = armedAt_ or e
    if e ~= nil and e.h ~= nil and at ~= nil then
      local text = string.format('Reminder: %s %02d:%02d', date_text(at.y, at.mo, at.d, kSmall, false), e.h, e.m)
      if ui.text_width(text, kSmall) <= wid - 2 * MARGIN then return text end
      -- **El nom del dia es queda i el mes es fa curt** si cal: what a reminder needs is the day, and the month is the one a
      -- person is looking at.
      return string.format('Reminder: %s %d %s %02d:%02d', DAY_SHORT[(agenda.weekdayOf(at.y, at.mo, at.d) or 0) + 1] or '?',
                           at.d, MONTH_SHORT[at.mo] or '?', e.h, e.m)
    end
  end

  if why_ ~= '' then return 'No reminder: ' .. why_ end
  return 'No reminder armed'
end

--- The form's own line: the date it is writing and what is chosen, built from the fields —una línia que diu què està a punt
-- de desar el formulari és l'única manera de veure el final d'un interval abans de desar-lo—.
local function form_text()
  local date = 'no date yet'
  if agenda.weekdayOf(formY_, formMo_, formD_) ~= nil then
    date = date_text(formY_, formMo_, formD_, kSmall, true)
  end

  local when
  if all_day_ then
    when = 'all day'
  elseif hour_pick_ == 0 then
    when = 'no hour yet'
  else
    local start = (hour_pick_ - 1) * 60 + ((min_pick_ > 0) and (min_pick_ - 1) or 0)
    local len = LENGTHS[len_pick_] or 0
    when = string.format('%02d:%02d', math.floor(start / 60), start % 60)
    if len > 0 then
      local over = start + len
      when = when .. string.format('-%02d:%02d', math.floor(over / 60), over % 60)
    end
  end

  return date .. ', ' .. when
end

--- The lines of the day screen: the date, and what the list is showing.
local function day_texts()
  if dayTitle ~= nil then dayTitle.text = date_text(seenY_, seenMo_, day_, kBody, true) end

  local hint = hint_
  if hint == '' then
    local n = #dayRows_
    if marked_ >= 0 and marked_ < n then
      local e = dayRows_[marked_ + 1].e
      hint = string.format('%s, %s. %s', agenda.RepeatName[e.rep] or 'once', (e.note ~= '' and e.note) or 'no notes',
                           'Change or Delete act on it.')
    elseif n == 0 then
      hint = 'Nothing on this day. New puts something in it.'
    elseif n > MaxRows then
      hint = string.format('%d events, the first %d are shown.', n, MaxRows)
    else
      hint = 'Tap an event to see it, or New.'
    end
  end

  if dayHint1 ~= nil and dayHint2 ~= nil then
    dayHint1.text, dayHint2.text = two_lines(hint, wid - 2 * MARGIN, kSmall)
  end
end

-- ============================================================== the file and the day

local function reload_all()
  local list, extra = agenda.load(agenda_file)
  if list ~= nil then
    events_ = list
    bad_ = extra or 0
    problem_ = ''
  else
    -- **Un fitxer que no es pot llegir no és una agenda buida**: dir «cap event» d'un fitxer que hi és seria convidar a
    -- escriure a sobre del que no s'ha pogut llegir.
    events_ = {}
    bad_ = 0
    problem_ = 'Could not read ' .. agenda.File .. ': ' .. tostring(extra)
  end
end

local function save_events()
  local ok, why = agenda.save(agenda_file, events_)
  if ok then return true end
  hint_ = 'Could not save: ' .. tostring(why)
  reload_all()
  return false
end

--- The days of the month on the glass that have something, marked on the calendar with a dot.
local function mark_month()
  if cal == nil then return end
  if seenY_ == 0 then
    cal.marks = {}
    return
  end
  cal.marks = agenda.daysWithSomething(events_, seenY_, seenMo_)
end

-- ============================================================== the reminder

local function cancel_mine()
  local ok, mine = pcall(alarm.list)
  if not ok or type(mine) ~= 'table' then return end
  -- **Només les seves**: `alarm.list()` és la cua del sistema —la de la `battery` i la del `meteo` també hi són— i anul·lar-ne
  -- una que no és d'aquesta app només deixa una línia d'avís al registre (docs/30).
  for _, one in ipairs(mine) do
    if type(one) == 'table' and one.id ~= nil and one.app == app.id then pcall(alarm.cancel, one.id) end
  end
end

-- **S'ARMA EL QUE AQUESTA APP TÉ, I EN TÉ UN**: the next event, whichever it is. The system has no "replace this one", so
-- what changes is asked for again from the start, and only the promises of this app (the `app` field of the queue).
local function arm_next()
  cancel_mine()

  armed_, armedAt_, why_ = 0, nil, ''

  local best, ms, far = agenda.soonest(events_, time.date())

  if best ~= nil and ms ~= nil then
    -- **El payload diu QUINA ocurrència i no el mot «remind»** (0.190.7): un `raise` obre la pàgina de zero, o sigui que el
    -- ganxo es troba `armed_` a zero i l'única cosa que sap del que ha sonat és el que l'alarma porta a dins. Amb la data, el
    -- ganxo obre el dia que ha sonat encara que la pàgina s'hagi construït fa un moment (docs/30, docs/60).
    -- **`time.date` diu els noms de la seva taula** (`year`, `month`, `day`) i l'event diu els seus (`y`, `mo`, `d`): es
    -- normalitzen aquí perquè la resta del fitxer només en conegui un, i un `day.y` sobre la taula del rellotge és un `nil`
    -- que peta al `format` (trobat a l'emulador, 18/11).
    local at = nil
    local now = time.now()
    if now ~= nil and now > 0 then
      local t = time.date(now + math.floor(ms / 1000))
      if t ~= nil and t.year ~= nil and t.year > 0 then at = { y = t.year, mo = t.month, d = t.day } end
    end
    local day = at or events_[best]
    local payload = string.format('%04d-%02d-%02d', day.y, day.mo, day.d)

    -- **`raise` perquè l'avís arribi**: una promesa que obre la pàgina és l'única manera que aquest sistema té d'ensenyar una
    -- cosa a l'hora dita; sense això l'avís només passaria per una finestra que ningú veu (docs/30, docs/60).
    local ok = pcall(alarm.at, { in_ms = math.floor(ms), payload = payload, raise = true, persist = true })
    if ok then
      armed_, armedAt_ = best, { y = day.y, mo = day.mo, d = day.d }
    else
      why_ = 'the system did not take the alarm'
    end
  end

  -- **Aquí una promesa no té el sostre de vint-i-quatre dies del projecte del paper**: `in_ms` és un enter de seixanta-quatre
  -- bits i el rellotge accepta una hora a mesos de distància, o sigui que l'avís es promet sencer i **la cadena per etapes no
  -- cal** —el `stage` del paper hi era perquè el seu rellotge comptava en mil·lisegons de trenta-dos bits—. El que queda és la
  -- línia que diu per què no s'ha pogut prometre, que és la part honesta (docs/30, docs/45, docs/60).
  if far and armed_ == 0 and why_ == '' then why_ = 'the next one is out of reach' end
end

-- ============================================================== the screens

local function give_back()
  if card ~= nil then
    pcall(card.release, card)
    card = nil
  end
  if screen ~= nil then
    pcall(screen.release, screen)
    screen = nil
  end
  monthTitle, remindLine, monthHint1, monthHint2, cal = nil, nil, nil, nil, nil
  dayTitle, dayHint1, dayHint2, dayList = nil, nil, nil, nil
  newBtn, changeBtn, deleteBtn, backBtn = nil, nil, nil, nil
  titleBox, noteBox, allDayChk, dateBtn, saveBtn, cancelBtn = nil, nil, nil, nil, nil, nil
  hourSel, minSel, lenSel, repSel = nil, nil, nil, nil
  rowEntries_ = {}
end

--- The month `delta` months away, carrying the year when it crosses December: the calendar only says which month it shows,
-- so turning it from Lua is writing its two fields, and crossing the year is this arithmetic.
local function add_months(y, mo, delta)
  mo = mo + delta
  while mo < 1 do mo = mo + 12; y = y - 1 end
  while mo > 12 do mo = mo - 12; y = y + 1 end
  return y, mo
end

--- **El mes de la pantalla, mogut des de qualsevol lloc**: it is the ONE place that changes `seenY_` and `seenMo_`, so the
-- calendar and its marks cannot come to disagree.
local function show_month_at(y, mo)
  if y == nil or mo == nil then return end
  if y < FirstYear or y > LastYear or mo < 1 or mo > 12 then return end

  seenY_, seenMo_ = y, mo
  if cal ~= nil then
    cal.year, cal.month = y, mo
    cal.selected = today_in(y, mo)
  end
  mark_month()
  paint()
  -- **El gir del mes demana el rentat de setze nivells**: el calendari sencer és tinta nova sobre la del mes d'abans i la
  -- passada barata deixa el fantasma dels números vells a sota dels nous (docs/50, docs/90).
  ui.present('gl16')
end

local function take_room()
  local c = ui.caps()
  wid, hei = c.width, c.height
end

local function root_panel(gap)
  local root = ui.panel(wid, hei, 'vertical')
  if root == nil then return nil end
  root.padding = MARGIN
  root.gap = gap or GAP
  root:place(0, 0)
  return root
end

-- **El que la pàgina diu**, que és el text de cada línia que no és d'un control.
paint = function()
  if screen == nil then return end

  if page_ == 1 then
    if monthTitle ~= nil then monthTitle.text = month_text() end
    if remindLine ~= nil then remindLine.text = remind_text() end
    if monthHint1 ~= nil and monthHint2 ~= nil then
      local hint
      if picking_ then
        -- **Mentre el mes és el triador de la data, ho diu**: the file's line and the count are not what a finger needs to know
        -- here, and a screen that did not say what it is waiting for would look hung.
        hint = 'Tap the day of the event. It goes back to the form.'
      else
        hint = problem_
        if hint == '' then hint = where_text() end
      end
      monthHint1.text, monthHint2.text = two_lines(hint, wid - 2 * MARGIN, kSmall)
    end
    if cal ~= nil then
      cal.today = today_in(seenY_, seenMo_)
      if seenY_ > 0 then
        cal.year, cal.month = seenY_, seenMo_
      end
    end
  elseif page_ == 2 then
    day_texts()
  else
    if dayHint1 ~= nil and dayHint2 ~= nil then
      local hint = hint_
      if hint == '' then
        local extra = ''
        if editing_ > 0 then
          local e = events_[editing_]
          if e ~= nil and e.rep ~= agenda.Once then extra = ' A change is for all its days.' end
        end
        hint = form_text() .. extra
      end
      dayHint1.text, dayHint2.text = two_lines(hint, wid - 2 * MARGIN, kSmall)
    end
  end
end

local shape_of
shape_state = function() return shape_of() end

-- ------------------------------------------------------------------ the month

open_month = function(y, mo)
  if y ~= nil then seenY_ = y end
  if mo ~= nil then seenMo_ = mo end

  page_, marked_, hint_ = 1, -1, ''

  give_back()
  take_room()
  ui.clear(PAPER)

  local root = root_panel(6)
  if root == nil then return end
  screen = root

  monthTitle = make_label(wid - 2 * MARGIN, line(kTitle), '', kTitle, 'left')
  if monthTitle == nil then return end
  root:add(monthTitle)

  remindLine = make_label(wid - 2 * MARGIN, line(kSmall), '', kSmall, 'left')
  if remindLine == nil then return end
  root:add(remindLine)

  -- **Les caselles del calendari són quadrades**: el control reparteix la seva caixa en set files —una pels noms dels dies i
  -- sis per a les setmanes— i una caixa alta fa els dies llargs i separats; **el que el projecte del paper dibuixava eren
  -- caselles de l'amplada d'una columna** (docs/30, docs/45).
  cal = ui.month(wid - 2 * MARGIN, wid - 2 * MARGIN)
  if cal == nil then
    console.print('agenda: no calendar for the month')
    return
  end
  cal.year = (seenY_ > 0) and seenY_ or FirstYear
  cal.month = (seenMo_ > 0) and seenMo_ or 1
  cal.selected = today_in(cal.year, cal.month)
  cal.weekdays = WEEKDAYS
  cal.scale = kBody
  cal.on_pick = function(self, day)
    -- **El dia es tria, i què vol dir depèn de la pantalla**: on the month it opens that day, and while the form has sent us
    -- here with `Date` it fills the form's date and goes back to the form.
    if day == nil or day <= 0 then return end
    if picking_ then
      formY_, formMo_, formD_ = cal.year, cal.month, day
      picking_ = false
      open_form(editing_, false, true)   -- keep: la data és la que s'acaba de triar
      return
    end
    open_day(day)          -- the month opens the day on top of itself
  end
  root:add(cal)

  -- **El que sobra és un buit i no un calendari estirat**: the hints sit at the bottom of the page as they did in the paper.
  local spring = ui.label(wid - 2 * MARGIN, 4)
  if spring ~= nil then
    spring.weight = 1
    spring.text = ''
    root:add(spring)
  end

  monthHint1 = make_label(wid - 2 * MARGIN, line(kSmall), '', kSmall, 'left')
  if monthHint1 == nil then return end
  root:add(monthHint1)

  monthHint2 = make_label(wid - 2 * MARGIN, line(kSmall), '', kSmall, 'left')
  if monthHint2 == nil then return end
  root:add(monthHint2)

  mark_month()
  paint()
  -- **El gir del mes demana el rentat de setze nivells**: el calendari sencer és tinta nova sobre la del mes d'abans i la
  -- passada barata deixa el fantasma dels números vells a sota dels nous (docs/50, docs/90).
  ui.present('gl16')
end

-- ------------------------------------------------------------------ the day

local function reload_day()
  rowEntries_ = {}
  if dayList == nil then return end

  dayRows_ = agenda.ofDay(events_, seenY_, seenMo_, day_)

  for index = 1, math.min(#dayRows_, MaxRows) do
    local entry = ui.list_entry(wid - 2 * MARGIN, 56)
    if entry == nil then break end

    local words = make_label(wid - 2 * MARGIN - 24, 42, agenda.rowOf(dayRows_[index].e), kBody, 'left')
    if words ~= nil then
      words:place(12, 7)
      entry:add(words)
    end

    entry.marked = (index - 1 == marked_)
    -- **La marca es mou sense tornar a fer la pàgina**: the entries are this app's controls and the list does not say which
    -- one a finger pressed; cada fila ho diu ella i la que hi era es desdiu.
    entry.on_click = function()
      marked_ = index - 1
      hint_ = ''
      for i = 1, #rowEntries_ do
        if rowEntries_[i] ~= nil then rowEntries_[i].marked = (i - 1 == marked_) end
      end
      paint()
      ui.present()
    end

    dayList:add(entry)
    rowEntries_[index] = entry
  end
end

--- **El dia, i si és una vista nova**: `push` és cert quan el dia s'obre des del mes —una pàgina a sobre d'una altra— i fals
-- quan el que es fa és tornar-lo a fer sobre la que ja hi és (tornar del formulari, esborrar, el pessic).
open_day = function(d, push)
  if seenY_ == 0 or d == nil or d < 1 then return end
  if d > agenda.daysInMonth(seenY_, seenMo_) then return end

  -- **La marca de la fila es queda si el dia és el mateix**: tornar del formulari al dia que s'estava mirant no és perdre
  -- quina fila tenia la marca.
  if d ~= day_ then marked_ = -1 end
  day_, page_, hint_ = d, 2, ''

  give_back()
  take_room()
  ui.clear(PAPER)

  local root = root_panel(GAP)
  if root == nil then return end
  screen = root

  dayTitle = make_label(wid - 2 * MARGIN, line(kBody), '', kBody, 'left')
  if dayTitle == nil then return end
  root:add(dayTitle)

  dayList = ui.list(wid - 2 * MARGIN, 200)
  if dayList == nil then
    console.print('agenda: no list for the events of the day')
    return
  end
  dayList.weight = 1
  dayList.row_height = 56
  root:add(dayList)

  dayHint1 = make_label(wid - 2 * MARGIN, line(kSmall), '', kSmall, 'left')
  if dayHint1 == nil then return end
  root:add(dayHint1)

  dayHint2 = make_label(wid - 2 * MARGIN, line(kSmall), '', kSmall, 'left')
  if dayHint2 == nil then return end
  root:add(dayHint2)

  -- **Els quatre botons, un per línia**, que és com els té l'original: el dia té la pàgina sencera i una fila de quatre
  -- tecles de seixanta-tres píxels no hi cap amb els dits d'una mà.
  newBtn = ui.button(wid - 2 * MARGIN, 63)
  if newBtn ~= nil then
    newBtn.title = 'New'
    newBtn.on_click = function() open_form(0) end
    root:add(newBtn)
  end

  local function mark_first()
    hint_ = 'Mark an event first: tap the row it is in.'
    paint()
    ui.present()
    return (marked_ >= 0 and marked_ < #dayRows_)
  end

  changeBtn = ui.button(wid - 2 * MARGIN, 63)
  if changeBtn ~= nil then
    changeBtn.title = 'Change'
    changeBtn.on_click = function()
      if not mark_first() then return end
      open_form(dayRows_[marked_ + 1].i)
    end
    root:add(changeBtn)
  end

  deleteBtn = ui.button(wid - 2 * MARGIN, 63)
  if deleteBtn ~= nil then
    deleteBtn.title = 'Delete'
    deleteBtn.on_click = function()
      if not mark_first() then return end

      -- **L'event es treu de la llista i el fitxer s'escriu**: no hi ha cap segona còpia enlloc, o sigui que el que no s'ha
      -- escrit no ha passat.
      local i = dayRows_[marked_ + 1].i
      table.remove(events_, i)

      local said = save_events() and 'The event is gone.' or hint_
      arm_next()
      open_day(day_, false)
      if said ~= nil then
        hint_ = said
        paint()
        ui.present()
      end
    end
    root:add(deleteBtn)
  end

  backBtn = ui.button(wid - 2 * MARGIN, 63)
  if backBtn ~= nil then
    backBtn.title = 'Back'
    backBtn.on_click = function()
      marked_ = -1
      app.view_pop()
      open_month(seenY_, seenMo_)
    end
    root:add(backBtn)
  end

  reload_day()
  paint()
  ui.present()

  if push ~= false then app.view_push() end
end

-- ------------------------------------------------------------------ the form

local function close_card()
  if card == nil then return end
  pcall(card.release, card)
  card = nil
  -- **El camp que ha obert la targeta torna a ser un camp**: el sistema posa `choosing` quan crida `on_open` i tornar-la a
  -- apagar és de l'app, també quan la targeta es tanca amb un toc fora d'ella (docs/30, docs/90).
  if card_field ~= nil then card_field.choosing = false end
  card_field = nil
end

local function pick_from(selector, names, keep)
  close_card()

  local box = ui.picker(wid - 24, hei - line(kTitle) - 40)
  if box == nil then return end

  box.title = selector.label
  box.items = names
  box.index = selector.index or 0
  box.on_pick = function(self, index)
    selector.index = index
    selector.choosing = false
    keep(index)
    close_card()
    paint()
    -- **La targeta és mitja pàgina de tinta i el formulari torna a sota**: el rentat de setze nivells és el que fa que el
    -- que queda al vidre sigui el formulari i no la seva ombra (docs/50, docs/90).
    ui.present('gl16')
  end
  box:place(12, line(kTitle) + 20)
  card, card_field = box, selector
  -- **El cercle va a la targeta**: mentre és al vidre és l'únic lloc on paren el dial i els dits, i el camp de sota no
  -- respon res. Quan la targeta marxa, el cercle torna sol (docs/30, docs/90).
  ui.focus(box)
  ui.present('gl16')
end

open_form = function(which, push, keep)
  local e = nil
  if which ~= nil and which > 0 then
    e = events_[which]
    if e == nil then return end
  end

  if which ~= nil and which > 0 then editing_ = which elseif which == 0 then editing_ = 0 end
  page_, hint_ = 3, ''

  if not keep then
    -- **La data és la del dia que s'ha obert el formulari** fins que algú en tria una altra amb `Date`, i per això una
    -- tornada del calendari no la torna a escriure: el qui la sap és el triador, no el dia que hi ha a sota (docs/45).
    formY_ = e and e.y or seenY_
    formMo_ = e and e.mo or seenMo_
    formD_ = e and e.d or day_
  end

  if keep then
    -- **El formulari torna amb les seves paraules**, que són el que la persona hi havia escrit i són al bloc `state`.
  elseif which ~= nil then
    title_ = e and e.title or ''
    note_ = e and (e.note or '') or ''

    -- **Un camp que ningú ha tocat és al guió**, que és la manera del control de dir «cap valor»: l'hora s'ha de triar, i el
    -- minut comença a zero perquè un minut de zero és un minut.
    hour_pick_ = (e and e.h and (e.h + 1)) or 0
    min_pick_ = (e and e.m and (e.m + 1)) or 1

    -- **La durada**: la que l'event ja tenia si és una de les tries, i el final que tenia guardat a part —un `13:30-14:20`
    -- escrit a mà no és cap durada d'aquesta llista, i una edició que no toca la durada no l'ha de moure—.
    lenTouched_ = false
    editEnd_ = (e and e.eh) and { h = e.eh, m = e.em } or nil

    local len = 1
    if e and e.eh then
      local span = (e.eh * 60 + e.em) - (e.h * 60 + e.m)
      for i = 1, #LENGTHS do
        if LENGTHS[i] == span then len = i end
      end
    end
    len_pick_ = len

    local rep = 1
    if e then
      for i = 1, #agenda.Repeats do
        if agenda.Repeats[i] == e.rep then rep = i end
      end
    end
    rep_pick_ = rep

    all_day_ = (e ~= nil) and (e.h == nil) or false
  end

  give_back()
  take_room()
  ui.clear(PAPER)

  local root = root_panel(6)
  if root == nil then return end
  screen = root

  -- **Les nou files es reparteixen el que queda i no tenen una alçada escrita al fitxer**, i el que queda és el de la vista
  -- **amb la banda del teclat al vidre**: si el formulari es fes per a la pàgina sencera, obrir una caixa l'hauria de tornar
  -- a fer —i una pàgina tornada a fer deixa el teclat escrivint en una caixa que ja no és al vidre—. `kb:height()` diu la
  -- banda que prendrà, i amb aquest número el formulari cap igual abans i després (docs/30, docs/45).
  local band = (band_h > 0) and band_h or kBandHeight
  do
    -- **I si la banda diu el que fa, mana ella**: `kb:height()` respon zero mentre el teclat no és al vidre i el número de
    -- veritat quan hi és, o sigui que el que s'ha mesurat una vegada és el que val (docs/30).
    local ok, kb = pcall(ui.keyboard)
    if ok and kb ~= nil then
      local h_ok, h = pcall(function() return kb:height() end)
      if h_ok and type(h) == 'number' and h > 0 then band = h end
    end
  end

  local rows = 9
  local hint_h = line(kSmall) * 2
  local room = hei - band
  if room < 320 then room = hei end
  local row_h = (room - 2 * MARGIN - hint_h - (rows - 1) * 6 - 6) // rows
  if row_h > 64 then row_h = 64 end
  if row_h < 44 then row_h = 44 end
  console.print(string.format('agenda: the form is laid out in %d px: the band takes %d of %d and a row is %d',
                              room, band, hei, row_h))

  titleBox = ui.textbox(wid - 2 * MARGIN, row_h)
  if titleBox == nil then return end
  titleBox.text = title_
  root:add(titleBox)

  noteBox = ui.textbox(wid - 2 * MARGIN, row_h)
  if noteBox == nil then return end
  noteBox.text = note_
  root:add(noteBox)

  -- `All day` i `Date` comparteixen fila: el formulari ha de cabre amb el teclat obert.
  local dateRow = ui.panel(wid - 2 * MARGIN, row_h, 'horizontal')
  if dateRow == nil then return end
  dateRow.gap = 8
  root:add(dateRow)

  local check_w = (wid - 2 * MARGIN) * 60 // 100
  allDayChk = ui.checkbox(check_w, row_h)
  if allDayChk ~= nil then
    -- **Les paraules d'una casella són un fill i no un camp**: el control dibuixa la càpsula i el seu lloc, i el que una
    -- persona llegeix al costat és una etiqueta de l'app (docs/30, docs/92).
    local words = make_label(check_w - 100, line(kBody) + 6, 'All day', kBody, 'left')
    if words ~= nil then
      words:place(92, (row_h - line(kBody) - 6) // 2)
      allDayChk:add(words)
    end
    allDayChk.checked = all_day_
    allDayChk.on_change = function(self, on)
      all_day_ = on and true or false
      -- **El que `All day` canvia**: l'hora, el minut i la durada deixen de ser botons, perquè un event sense hora no té
      -- moment i un control apagat ho diu sense que ningú ho expliqui. Els valors es queden on són.
      -- **Un control apagat és `disabled`** i no una paraula inventada (0.100.0): es dibuixa en el to fluix i el dial, el dit
      -- i el centre no hi arriben, que és el que l'original deia amb `set_enabled` (docs/30, docs/92).
      if hourSel ~= nil then hourSel.disabled = all_day_ end
      if minSel ~= nil then minSel.disabled = all_day_ end
      if lenSel ~= nil then lenSel.disabled = all_day_ end
      paint()
      ui.present()
    end
    dateRow:add(allDayChk)
  end

  dateBtn = ui.button((wid - 2 * MARGIN) - check_w - 8, row_h)
  if dateBtn ~= nil then
    dateBtn.title = 'Date'
    dateBtn.weight = 1
    dateBtn.on_click = function()
      -- **El mes és el triador de la data**, i això és tot: hi va amb el dia del formulari triat, i el calendari sap que
      -- està triant perquè `picking_` ho diu. La vista no canvia, o sigui que el pessic d'aquí torna al formulari.
      if titleBox ~= nil then title_ = titleBox.text or '' end
      if noteBox ~= nil then note_ = noteBox.text or '' end
      picking_ = true
      open_month(formY_, formMo_)
      if cal ~= nil then
        cal.selected = formD_
        mark_month()
        paint()
        ui.present()
      end
    end
    dateRow:add(dateBtn)
  end

  local function a_selector(label_text, names, index, keep)
    local one = ui.selector(wid - 2 * MARGIN, row_h)
    if one == nil then return nil end
    one.label = label_text
    one.items = names
    one.index = index
    one.empty = '-'
    one.on_open = function(self)
      -- **El camp deixa el desig i el bucle obre la targeta**: obrir-la aquí seria obrir-la dins del toc que la demana
      -- (docs/30, docs/45).
      wanted_ = function() pick_from(self, names, keep) end
    end
    root:add(one)
    return one
  end

  hourSel = a_selector('Hour', HOURS, hour_pick_, function(index) hour_pick_ = index end)
  minSel = a_selector('Minute', MINUTES, min_pick_, function(index) min_pick_ = index end)
  lenSel = a_selector('Length', LENGTH_NAMES, len_pick_, function(index)
    len_pick_ = index
    lenTouched_ = true
  end)
  repSel = a_selector('Repeats', REPEAT_NAMES, rep_pick_, function(index) rep_pick_ = index end)

  if all_day_ then
    if hourSel ~= nil then hourSel.disabled = true end
    if minSel ~= nil then minSel.disabled = true end
    if lenSel ~= nil then lenSel.disabled = true end
  end

  local button_h = (row_h < 63) and row_h or 63

  saveBtn = ui.button(wid - 2 * MARGIN, button_h)
  if saveBtn ~= nil then
    saveBtn.title = 'Save'
    saveBtn.on_click = function() save_form() end
    root:add(saveBtn)
  end

  cancelBtn = ui.button(wid - 2 * MARGIN, button_h)
  if cancelBtn ~= nil then
    cancelBtn.title = 'Cancel'
    cancelBtn.on_click = function()
      picking_ = false
      hint_ = ''
      app.view_pop()
      open_day(day_, false)
    end
    root:add(cancelBtn)
  end

  dayHint1 = make_label(wid - 2 * MARGIN, line(kSmall), '', kSmall, 'left')
  if dayHint1 == nil then return end
  root:add(dayHint1)

  dayHint2 = make_label(wid - 2 * MARGIN, line(kSmall), '', kSmall, 'left')
  if dayHint2 == nil then return end
  root:add(dayHint2)

  paint()
  ui.present()

  if push ~= false then app.view_push() end
end

--- **El que no pot ser un event es refusa aquí i es diu aquí**, i cada refús té una frase: les dues coses que el fitxer no
-- pot guardar, l'hora que ningú ha triat, la data que el calendari no té, i el final que creuaria la mitjanit.
save_form = function()
  if titleBox ~= nil then title_ = titleBox.text or '' end
  if noteBox ~= nil then note_ = noteBox.text or '' end

  local rep = agenda.Repeats[rep_pick_] or agenda.Once

  if title_ == '' then
    hint_ = 'An event needs a name.'
  elseif title_:find('|', 1, true) or note_:find('|', 1, true) then
    hint_ = "'|' cannot be written: it separates the fields of the file."
  elseif not all_day_ and hour_pick_ == 0 then
    hint_ = 'The hour has to be chosen.'
  elseif formD_ > agenda.daysInMonth(formY_, formMo_) then
    hint_ = 'That date does not exist.'
  else
    local endH, endM = nil, nil
    local late = false

    if not all_day_ then
      local len = LENGTHS[len_pick_] or 0
      if len > 0 then
        local over = (hour_pick_ - 1) * 60 + ((min_pick_ > 0) and (min_pick_ - 1) or 0) + len
        if over > 23 * 60 + 59 then
          late = true
        else
          endH, endM = math.floor(over / 60), over % 60
        end
      elseif not lenTouched_ and editEnd_ ~= nil then
        -- **El final que tenia una línia escrita a mà**, que no és cap de les tries.
        endH, endM = editEnd_.h, editEnd_.m
      end
    end

    if late then
      hint_ = 'An event cannot end after midnight.'
    else
      local e = { y = formY_, mo = formMo_, d = formD_, rep = rep, title = title_, note = note_ }
      if not all_day_ then
        e.h, e.m = hour_pick_ - 1, (min_pick_ > 0) and (min_pick_ - 1) or 0
        e.eh, e.em = endH, endM
      end

      if editing_ > 0 then
        events_[editing_] = e
      else
        events_[#events_ + 1] = e
      end

      if save_events() then
        -- **La pantalla va al dia que s'ha desat**, que no sempre és el que estava obert: un event amb una altra data es
        -- desaria i després no es veuria, que sembla exactament un desat que no ha passat.
        seenY_, seenMo_, day_ = e.y, e.mo, e.d
        marked_, hint_ = -1, 'Saved.'
        formY_, formMo_, formD_ = e.y, e.mo, e.d
        arm_next()
        app.view_pop()
        open_day(day_, false)
        -- the line is said on the day the save landed on
        hint_ = 'Saved.'
        paint()
        ui.present()
        return
      end
    end
  end

  paint()
  ui.present()
end

-- ============================================================== the shape of the page, and the hooks

shape_of = function()
  return {
    page = page_,
    day = day_,
    y = seenY_,
    mo = seenMo_,
    marked = (marked_ >= 0) and (marked_ + 1) or nil,
    editing = editing_,
    hour = hour_pick_,
    min = min_pick_,
    len = len_pick_,
    rep = rep_pick_,
    formy = formY_,
    formmo = formMo_,
    formd = formD_,
    text = { title = title_, note = note_ },
    check = { all_day = all_day_ },
  }
end

--- **Una promesa crida aquest ganxo hi hagi pàgina o no** (0.189.145): it cannot wait and it cannot paint a page that may not
-- exist. It says which event rang and arms the next one, and the turn of the page opens that day.
--
-- **I el que ha sonat es llegeix del payload i no de `armed_`** (0.190.7): una alarma amb `raise` obre la pàgina de zero —el
-- bloc d'adalt només corre fins al guarda— i quan el ganxo arriba `armed_` és el pròxim event i no el que ha sonat. El payload
-- porta el dia de l'ocurrència, que és l'única cosa que sobreviu al fet de néixer la pàgina fa un moment (docs/30, docs/60).
function on_alarm(id, payload, meta)
  local text = tostring(payload or '')
  local y, mo, d = text:match('^(%d%d%d%d)-(%d%d)-(%d%d)$')
  if y == nil and text ~= 'remind' and (meta == nil or meta.missed == nil) then return end

  if y ~= nil then
    remindAt_ = { y = tonumber(y), mo = tonumber(mo), d = tonumber(d) }
  else
    -- **L'alarma d'una versió anterior porta el mot i no la data**: es queda amb el que l'app sap, que és el pròxim event.
    local e = events_[armed_]
    if e ~= nil then remindAt_ = { y = e.y, mo = e.mo, d = e.d } end
  end
  reload_all()
  arm_next()
end

--- **El pessic que torna una pàgina**: the system has already taken the view away, so what is left is putting the screen of
-- that depth on the glass again.
function on_back(depth)
  picking_ = false
  if depth >= 1 then
    -- **La pàgina que queda és la del dia**: el sistema ja ha tret la vista de sobre, o sigui que el que es fa és tornar a fer
    -- el dia i no empènyer-ne un de nou.
    open_day(day_, false)
  else
    marked_ = -1
    open_month(seenY_, seenMo_)
  end
end

--- **Les tres portes del comiat**, i van abans del bucle: a chunk that never ends registers nothing that comes after it.
function on_pause() save_state(shape_state()) end
function on_save() save_state(shape_state()) end
function on_close() save_state(shape_state()) end

-- **Una finestra només porta la crida de l'alarma** (0.189.145): the system loads the entry to open the app and calls the
-- hook right away, and without this door every window would build a page nobody sees.
if not console.is_foreground() then return end

-- ============================================================== the page the person sees

-- **La forma que la persona va deixar, llegida abans de col·locar res** (docs/30, docs/45): the screen, the day, the month the
-- calendar was showing, the row with the mark, the form's words and its fields. A value that is not one is dropped here.
do
  local was = load_state()
  if was ~= nil then
    local function number(key, low, high)
      local value = tonumber(was[key])
      if value == nil then return nil end
      value = math.floor(value)
      if value < low or value > high then return nil end
      return value
    end

    page_ = number('page', 1, 3) or page_
    day_ = number('day', 1, 31) or day_
    local y = number('y', FirstYear, LastYear)
    local mo = number('mo', 1, 12)
    if y ~= nil and mo ~= nil then seenY_, seenMo_ = y, mo end
    local marked = number('marked', 1, MaxRows)
    if marked ~= nil then marked_ = marked - 1 end
    editing_ = number('editing', 0, 9999) or editing_
    hour_pick_ = number('hour', 0, 24) or hour_pick_
    min_pick_ = number('min', 1, 60) or min_pick_
    len_pick_ = number('len', 1, #LENGTHS) or len_pick_
    rep_pick_ = number('rep', 1, #REPEAT_NAMES) or rep_pick_
    formY_ = number('formy', FirstYear, LastYear) or formY_
    formMo_ = number('formmo', 1, 12) or formMo_
    formD_ = number('formd', 1, 31) or formD_
    if was.check ~= nil then all_day_ = was.check.all_day and true or false end
    if was.text ~= nil then
      title_ = was.text.title or ''
      note_ = was.text.note or ''
    end
    console.print(string.format('agenda: came back to the %s screen',
                                (page_ == 3) and 'form' or ((page_ == 2) and 'day' or 'month')))
  end
end

-- **The clock and the file**, which is what the page needs before it is drawn. A clock that is not set leaves the calendar at
-- the edge of the years it may show and the title says so.
do
  local now = time.date()
  if now ~= nil and now.year ~= nil and now.month ~= nil then
    if seenY_ == 0 then seenY_, seenMo_ = now.year, now.month end
    if day_ == 0 then day_ = now.day or 1 end
    if formY_ == 0 then formY_, formMo_, formD_ = seenY_, seenMo_, day_ end
  end
  if seenY_ == 0 then
    seenY_, seenMo_ = FirstYear, 1
    day_ = 1
  end
  if formY_ == 0 then formY_, formMo_, formD_ = seenY_, seenMo_, day_ end
end

reload_all()
arm_next()
save_state(shape_state())

console.print(string.format('agenda: %d event(s) in %s, %s', #events_, agenda_file,
                            (armed_ > 0) and 'a reminder is armed' or 'nothing armed'))

if page_ == 3 then
  open_day(day_)
  open_form(editing_, true, true)   -- keep: the words and the fields are the state's
elseif page_ == 2 then
  open_month(seenY_, seenMo_)
  open_day(day_)
else
  open_month(seenY_, seenMo_)
end

-- **El dia que ha sonat s'obre sol**: the hook has left the day and the turn of the page does the rest. **I és el dia de
-- l'ocurrència, no el de la primera vegada**: un event que torna cada setmana sona el dia que toca (0.190.7, docs/30).
local function open_reminded()
  if remindAt_ == nil then return end
  local at = remindAt_
  remindAt_ = nil
  seenY_, seenMo_, day_ = at.y, at.mo, at.d
  open_day(at.d)
end

local was_wid, was_hei = wid, hei

while true do
  -- **Un canvi de mida és el teclat que ve o que marxa, i la pàgina s'adapta i no es torna a fer**: a page built again makes
  -- new boxes, and the band the finger opened would be left writing to a box that is not on the glass any more. Un panel
  -- sense píxels propis es pot redimensionar i el repartiment el torna a fer ell al següent pintat (docs/30).
  local c = ui.caps()
  if c.width ~= was_wid or c.height ~= was_hei then
    -- **El que la vista ha perdut és la banda**, i es queda per al pròxim formulari: `kb:height()` no ho diu fins que la
    -- banda és al vidre, i el que s'ha de fer abans és el formulari de la mida que quedarà (docs/30).
    if c.height < was_hei and band_h == 0 then band_h = was_hei - c.height end
    was_wid, was_hei = c.width, c.height
    wid, hei = c.width, c.height
    if screen ~= nil then pcall(screen.resize, screen, wid, hei) end
    paint()
    ui.present()
  end

  open_reminded()

  -- **El desig d'un control**, que és el que ha demanat el dit al torn anterior: la targeta s'obre aquí i no dins del ganxo,
  -- perquè el sistema acaba d'escriure el camp després del ganxo i el pintaria a sobre de la targeta (docs/30, docs/45).
  if wanted_ ~= nil then
    local doing = wanted_
    wanted_ = nil
    sys.delay(300):await()
    doing()
  end

  -- **El gest del costat és el mes i el de dalt a baix és l'any**, i tots dos es responen aquí perquè un gest no arriba mai a
  -- l'arbre dels controls (docs/30): a swipe that is not taken here is a swipe that does nothing. Cap a l'esquerra és el mes
  -- següent i cap a la dreta l'anterior, com les pàgines d'una llista d'aquest sistema, i amunt i avall són l'any.
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'drag' then
      if card ~= nil then
        -- **Una targeta oberta es tanca amb un toc fora d'ella**, i el gest el llegeix la pàgina perquè una targeta del
        -- sistema és l'amo dels dits que hi cauen (docs/30).
        close_card()
        paint()
        ui.present('gl16')
      elseif page_ == 1 and cal ~= nil then
        local dx = gesture.dx or 0
        local dy = gesture.dy or 0
        local y = (seenY_ > 0) and seenY_ or FirstYear
        local mo = (seenMo_ > 0) and seenMo_ or 1
        if math.abs(dx) > 40 and math.abs(dx) > math.abs(dy) then
          local ny, nmo = add_months(y, mo, (dx < 0) and 1 or -1)
          show_month_at(ny, nmo)
        elseif math.abs(dy) > 40 then
          show_month_at(y + ((dy < 0) and 1 or -1), mo)
        end
      end
    end
    gesture = touch.gesture()
  end

  sys.delay(120):await()
end
