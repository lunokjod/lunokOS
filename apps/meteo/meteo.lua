-- meteo: what the room is like, and what it has been like. The port of the paper project's `Meteo` (0.189.169).
--
-- **L'INTERFÍCIE ÉS LA DE L'ORIGINAL** (l'amo: *«L'interficie ha de ser IGUAL, EXACTAMENT IGUAL, l'unica diferencia han de
-- ser els controls UI, que has d'usar els del sistema»*): the left half is a THERMOMETER with the value and its name, the
-- right half is the humidity as a NUMBER, the BARS of its history under it, the line of the window, the CHART of the
-- temperature with the minimum, the middle and the maximum of the window written in the band on the left, the three
-- buttons of the three windows and the two lines of the foot -«Read at 12:34 (5). Redraw on change;» / «log every 30
-- min.»-. What changes is what draws each of them: the thermometer and the bars are painted by hand -this system has no
-- instrument and no bar control, and an instrument is looked at and never touched-, the chart is `ui.chart` -the control
-- the `Battery` brought (0.189.144)- and the labels and the buttons are the system's (docs/30, docs/45).
--
-- **THE SHT30 IS READ BY THE APP'S ALARM, AND ONE SAMPLE EVERY THIRTY MINUTES IS KEPT** (the owner, 20/10: *«canvia les
-- mostres a cada 30 minuts»*): the reading is the alarm's because that is the only way this system calls an app that is
-- not in front of the glass, which is the whole point of a screen of the room. **I el que sona es fa dins del ganxo** (the
-- lesson of the 0.189.145): a window calls `on_alarm` and destroys the machine right after, so the sensor, the sample and
-- the line of the log are done there, and the page only repaints what the hook wrote.
--
-- **AND THE GLASS IS REPAINTED ONLY IF SOMETHING CHANGED** (the owner, 20/10: *«redibuixa només si hi ha canvi en alguna de
-- les dades»*): a reading that finds the same temperature, the same humidity and no new sample is the same picture, and a
-- pass for it is a waveform bought for nothing. What the state watches is what the page paints, never the clock or the
-- number of readings.
--
-- **THE LOG IS A TEXT FILE, ONE LINE PER SAMPLE** —the minute of the epoch, the temperature in hundredths, the humidity in
-- whole percent: `35584271,2150,45`— and the samples live in two packed strings, **four bytes for the temperature and one
-- for the humidity**, because four thousand numbers in a Lua table is a rehash of sixty-four kilobytes and this app's
-- budget is the machine it runs in (docs/75). The samples are read from the file every time the app is mounted and they
-- are not in the `state` block: the block is the shape of the view and the log is history (docs/30).
--
-- **A HOLE IS A HOLE**: no sample for more than three hours -the board was off, the alarm did not come- stops the curve,
-- and so does a clock that went backwards. Joining the two sides would say the room went smoothly from one value to the
-- other, which nobody measured.

local caps = sys.caps()
if not caps.display then
  console.print('meteo: this board has no glass')
  return
end

-- **Una placa sense sensor d'aire arrenca igual, i ho diu abans de la primera lectura** (docs/30): refusing to open would hide
-- the samples that are already in the log —ninety days of the room worth looking at— over a chip that is not there, and the
-- foot of the glass says it again with the code when the reading fails. Aquí ho diu el registre, que és el que es llegeix de
-- dalt a baix quan una placa fa una cosa estranya.
if not caps.env then
  console.print('meteo: no ambient sensor on this board (E_UNSUPPORTED); only the log is shown')
end

-- **La cara del sistema no porta el grau** (the owner, 17/11: *«Fes-ho»*). `dejavusans-bold.ttf`, the face of every page, is
-- the printable ASCII plus the signs of a key, and the degree is **code 176 of Latin-1** —the same code the paper project's
-- bitmap faces carried, and the reason its thermometer could write `22.5 °C`—, o sigui que aquí el número s'acabava amb un
-- forat d'un caràcter. Aquesta cara és la mateixa família i el mateix pes amb el joc Latin-1 a dins
-- (`tools/ttf_latin1.py --set latin1`), i la pàgina la demana per a si mateixa: **els controls es queden la del sistema** i el
-- número del termòmetre, que és l'únic lloc on surt el grau, el pinta la pàgina amb aquesta (docs/30, docs/45).
local page_face = ui.load_font('/fonts/dejavusans-bold-latin1.ttf')
if page_face ~= nil then
  ui.font(page_face)
else
  console.print('meteo: no bold face with Latin-1 in /fonts; the degree will be a gap')
end

-- The four sizes of the paper project, in pixels of line (its `small` 33, `body` 43, `title` 57 and `big` 82), asked in
-- pixels and not in steps because the two faces of this system do not make the same line (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 8 then scale = 8 end

  return scale
end

local kSmall = scale_for(33)
local kTitle = scale_for(57)
local kBig = scale_for(82)

local function line(scale)
  return scale * ui.line_height(1)
end

local INK, PAPER = 0, 15

-- **Un text aquí és un byte per caràcter** (docs/30): the degree sign is the Latin-1 code 176 and not the two bytes of a
-- UTF-8 `°`, o sigui que `'\176'` és el que la cara de tornassol del disc dibuixa com una rodona i el que una cara que no
-- el porta deixa en un forat -un de sol i no dos-.
local DEGREE = string.char(176)

-- **La pàgina es rentja amb setze nivells i sense parpelleig** (the lesson of the `Battery`): the mercury moves and the
-- curve is made again whole at every sample, and a cheap pass leaves the ghost of the value before it -a column of mercury
-- two values tall reads as a value that is not there- (docs/50).
local kPageMode = 'gld16'
-- I una finestra nova és una pàgina nova: the scale, the axis and the whole curve change, and the two blinks of a `gc16`
-- are paid once, when a person asks for another window.
local kWindowMode = 'gc16'

-- ---------------------------------------------------------------- the numbers of the page

-- The air around everything, the air between two things, the two fixed rows of the foot. **El lloc de les barres i del gràfic
-- no és un número del fitxer** (the owner said at the same moment that the humidity is not as important as the temperature, so
-- its bars are a third of the chart): the two of them share what the four lines of text leave, and `layout()` is the one that
-- splits it —the numbers of the paper project are the lines of ITS screen— (docs/45).
local MARGIN, GAP = 20, 10
-- **La fila dels tres botons fa seixanta-tres píxels** com al projecte del paper, que és el que demana un botó d'aquest
-- sistema perquè un dit hi caigui a sobre.
local RangeH = 63
local NoteH = line(kSmall) * 2
-- **El termòmetre es queda nou dinovens de l'amplada** (the owner, 20/10: *«desplaça una mica el termòmetre a l'esquerra per
-- guanyar espai»*): the two halves are 9 and 10, which is what gives the two charts twelve pixels more.
local TempShare, RightShare = 9, 10

-- The thermometer's scale: a room is read between ten below zero and fifty above, and an instrument does not change its
-- scale because the week is warm. The chart is **not** the instrument (see `refresh`).
local TempLo, TempHi = -10, 50

-- **El rang físic del gràfic**, in tenths of a degree, and wide on purpose: `chart_draw` takes the two ends of the paper
-- from the samples of the window and only clamps them into this pair, so a wide pair is what makes the window's scale the
-- window's (-40.0 .. 125.0 °C is the SHT30's own range) (docs/30, docs/90).
local ChartLo, ChartHi = -400, 1250

-- The alarm reads the sensor every ten minutes; the log keeps one sample every thirty. The two are not the same clock: a
-- reading is cheap and the screen is looked at, a sample is history and ninety days of it has a size.
local EveryMs = 10 * 60 * 1000
local SampleMinutes = 30
local PerHour = 60 // SampleMinutes
local KeepDays, OverDays = 92, 2
local MaxSamples = 24 * KeepDays * PerHour
local HardSamples = 24 * (KeepDays + OverDays) * PerHour

-- The three windows: three months is ninety days and not a calendar quarter, because a window whose size changes with the
-- month is a window nobody can compare. `samples` is the fallback when there is no clock.
local Ranges = {
  { id = 'r24', label = '24 h', minutes = 24 * 60, samples = 48 },
  { id = 'r7d', label = '7 d', minutes = 7 * 24 * 60, samples = 7 * 48 },
  { id = 'r3mo', label = '3 mo', minutes = 90 * 24 * 60, samples = 90 * 48 },
}

-- The chart of this system draws forty-eight points at most, so a window with more samples is thinned to that. A long window
-- therefore loses the rule of the hole -two kept samples are already hours apart and the gap cannot tell the thinning from a
-- hole- and the 24 h one keeps it whole: three hours of silence is a hole in the curve (docs/30).
local MostPoints = 48

-- The log lives in the app's own ground (`fs.data`): an app writes only inside `/data/<id>` (docs/70). It is read in chunks
-- because the whole file is around eighty kilobytes and the interpreter's memory is the limit (docs/30).
local function log_path() return fs.data('meteo.log') end
local function settings_path() return fs.data('settings.txt') end
local ReadChunk = 2048

-- ---------------------------------------------------------------- the log, packed

-- Nineteen bits of minutes since the oldest sample of the run and twelve bits of temperature in steps of 0.05 from -40.00:
-- thirty-one bits, which is an integer this machine has. One byte per sample for the humidity, and 255 is «the line had
-- none» —a sample without a humidity is not a sample of 0 %.
local PackShift, TempBias, TempStep = 4096, 800, 5
local HumUnknown = 255
-- **El minut empaquetat té dinou bits** i són tres-cents seixanta-quatre dies: una mostra més vella que això no cap a la
-- cadena i marxa, que és el que fa que el sostre del registre sigui seu i no del rellotge.
local OldestMinutes = 364 * 1440

local packed_, humidity_ = '', ''
local base_ = nil
local buf, buf_n = {}, 0
local hbuf, hbuf_n = {}, 0

-- **Les cadenes es fan de trossos de dos-cents cinquanta-sis bytes** en llegir el fitxer: concatenar-hi una mostra quatre mil
-- vegades serien desenes de megabytes de còpia i de brossa (docs/75).
local function flush_packed()
  if buf_n == 0 then return end
  packed_ = packed_ .. string.char(table.unpack(buf, 1, buf_n))
  buf, buf_n = {}, 0
end

local function flush_humidity()
  if hbuf_n == 0 then return end
  humidity_ = humidity_ .. table.concat(hbuf)
  hbuf, hbuf_n = {}, 0
end

-- **El coixí només té les mostres que encara no són a la cadena** i tota lectura el buida abans: `packed_at` i `humidity_at`
-- compten només el que ja hi ha a `packed_`/`humidity_`, i per això el número de mostres també.
local function packed_count() return #packed_ // 4 end

local function packed_at(k)
  local a, b, c, d = packed_:byte(k * 4 - 3, k * 4)
  return a + b * 256 + c * 65536 + d * 16777216
end

local function minute_at(k) return base_ + packed_at(k) // PackShift end

-- **Els centèsims**, which is the number the paper project painted with: the chart of this system takes one decimal, so the
-- page divides by ten when it hands the series over.
local function centi_at(k) return (packed_at(k) % PackShift - TempBias) * TempStep end

local function humidity_at(k)
  local b = humidity_:byte(k)
  if b == nil or b == HumUnknown then return nil end
  return b
end

local function push_sample(minute, centi)
  if not base_ then base_ = minute end
  local d = minute - base_
  if d < 0 then d = 0 end

  local q = (centi + TempStep // 2) // TempStep + TempBias
  if q < 0 then q = 0 end
  if q > PackShift - 1 then q = PackShift - 1 end

  local p = d * PackShift + q
  buf[buf_n + 1] = p % 256
  buf[buf_n + 2] = (p // 256) % 256
  buf[buf_n + 3] = (p // 65536) % 256
  buf[buf_n + 4] = (p // 16777216) % 256
  buf_n = buf_n + 4
  if buf_n >= 256 then flush_packed() end
end

local function push_humidity(percent)
  local h = HumUnknown
  if percent ~= nil and percent >= 0 and percent <= 100 then h = math.floor(percent) end
  hbuf_n = hbuf_n + 1
  hbuf[hbuf_n] = string.char(h)
  if hbuf_n >= 256 then flush_humidity() end
end

-- The oldest samples go: the two strings lose their head with one `sub` and the base does not move (a re-base would rewrite
-- every sample that stays).
local function drop_oldest(n)
  flush_packed()
  flush_humidity()
  packed_ = packed_:sub(n * 4 + 1)
  humidity_ = humidity_:sub(n + 1)
  if #packed_ == 0 then base_ = nil end
end

-- One sample, as the line of the file.
local function line_of(k)
  local h = humidity_at(k)
  local text = minute_at(k) .. ',' .. centi_at(k)
  if h then text = text .. ',' .. h end
  return text .. '\n'
end

-- **El desat normal és una línia afegida i prou**: rewriting the whole file every half hour would be paying eighty kilobytes
-- of card for twenty bytes of data (docs/30, docs/70).
local log_why = ''
local log_read = false

local function append_line()
  local text = line_of(packed_count())
  local file, why = fs.open(log_path(), 'a')
  if file == nil then
    log_why = 'Could not save: ' .. tostring(why and why.code or why or 'no file')
    return false
  end
  local written = file:write(text)
  file:close()
  if type(written) ~= 'number' then
    log_why = 'Could not save: the log took no bytes'
    return false
  end
  log_why = ''
  return true
end

-- **L'escombratge és una sola reescriptura**, i `fs.write` és atòmica —escriu un temporal i el reanomena—, o sigui que una
-- escriptura que mor a mitges no deixa mig registre sota el nom del sencer (docs/30, docs/70).
local function rewrite_log()
  local pieces = {}
  local n = packed_count()
  for k = 1, n do pieces[#pieces + 1] = line_of(k) end
  local written, why = fs.write(log_path(), table.concat(pieces))
  if type(written) ~= 'number' then
    log_why = 'Could not save: ' .. tostring(why and why.code or why or 'no room')
    return false
  end
  log_why = ''
  return true
end

-- A log that cannot be read is never written over: the app says so and does not touch the file. A log that is not there is
-- not an error -the first run of this app has no history and the chart says so- (docs/30).
local function load_log()
  packed_, humidity_, base_ = '', '', nil
  buf, buf_n, hbuf, hbuf_n = {}, 0, {}, 0

  local file, why = fs.open(log_path(), 'r')
  if file == nil then
    if why == nil or why.code == 'E_NOENT' then return true end
    log_why = 'Could not read: ' .. tostring(why.code or why)
    return false
  end

  local carry = ''
  local swept = false

  while true do
    local chunk = file:read(ReadChunk)
    if chunk == nil or chunk == '' then break end

    local text = carry .. chunk
    local cut = text:find('\n[^\n]*$')
    local complete = (cut ~= nil) and text:sub(1, cut) or ''
    carry = (cut ~= nil) and text:sub(cut + 1) or text

    for a, b, c in complete:gmatch('(%-?%d+),(%-?%d+),?(%d*)') do
      local minute, centi = tonumber(a), tonumber(b)
      if minute ~= nil and centi ~= nil then
        push_sample(minute, centi)
        push_humidity(c ~= '' and tonumber(c) or nil)
      end
    end

    -- **Un registre més llarg que el sostre es poda mentre es llegeix**: a file of three years is not a page, and a string of
    -- a hundred kilobytes is not something this interpreter has room for (docs/75).
    if packed_count() > HardSamples then
      drop_oldest(packed_count() - MaxSamples)
      swept = true
    end
  end
  file:close()

  -- The last line of a file without its newline is a line too.
  if carry ~= '' then
    local a, b, c = carry:match('^(%-?%d+),(%-?%d+),?(%d*)$')
    if a ~= nil and b ~= nil then
      push_sample(tonumber(a), tonumber(b))
      push_humidity(c ~= '' and tonumber(c) or nil)
    end
  end

  flush_packed()
  flush_humidity()

  if packed_count() > HardSamples then
    drop_oldest(packed_count() - MaxSamples)
    swept = true
  end

  if swept then
    -- The file follows the samples that stayed: a log that is thinned in memory and whole on the card would be thinned all
    -- over again at the next opening (docs/30).
    rewrite_log()
  end

  return true
end

-- ---------------------------------------------------------------- the reading and the sample

local now_temp, now_humidity = nil, nil
local read_hour, read_minute = nil, nil
local reads = 0
local no_sensor, clock_why, alarm_why = '', '', ''

-- **El rellotge de la història és el minut de l'època**: `time.now()` is UTC and `time.date()` gives it back in the hour a
-- person reads, which is what the axis of the chart says (docs/30, docs/60).
local function minute_now()
  local seconds = time.now()
  if seconds == nil or seconds <= 0 then return nil end
  return math.floor(seconds / 60)
end

local function read_sensor()
  local sample, why = env.read()
  if sample == nil or sample.celsius == nil or sample.humidity == nil then
    -- **No sensor is not a zero**: the instrument stays empty and the line says why. An app that painted twenty degrees
    -- because it could not ask is an app that lies on a screen of measurements (docs/30).
    now_temp, now_humidity = nil, nil
    no_sensor = 'The SHT30 does not answer'
    console.print('meteo: ' .. no_sensor .. ' (' .. tostring(why and why.code or 'no reading') .. ')')
    return false
  end

  now_temp = math.floor(sample.celsius * 100 + 0.5)
  now_humidity = math.floor(sample.humidity * 100 + 0.5)
  reads = reads + 1
  no_sensor = ''

  local today = time.date()
  if today ~= nil then
    read_hour, read_minute = today.hour, today.min
  end

  clock_why = (today == nil or minute_now() == nil) and 'No clock: the readings are shown and not logged.' or ''
  return true
end

-- **One sample every half hour**, and it is written only when it can be placed in the log: a line without an hour cannot be
-- put in a window and a made-up hour is worse than no line. The same minute twice is the same piece of history, and a minute
-- before the last one (a clock corrected backwards) is not history either (docs/30).
local function take_sample()
  if not log_read or now_temp == nil then return false end

  local t = minute_now()
  if t == nil then return false end

  local n = packed_count()
  if n > 0 and minute_at(n) == t then return false end
  if n > 0 and (t - minute_at(n)) < SampleMinutes then return false end

  local too_old = 0
  while too_old < n and t - minute_at(too_old + 1) > OldestMinutes do too_old = too_old + 1 end
  if too_old > 0 then drop_oldest(too_old) end

  push_sample(t, now_temp)
  flush_packed()
  push_humidity(now_humidity ~= nil and math.floor(now_humidity / 100 + 0.5) or nil)
  flush_humidity()

  local swept = false
  if packed_count() > HardSamples then
    drop_oldest(packed_count() - MaxSamples)
    swept = true
  end

  local saved
  if swept then saved = rewrite_log() else saved = append_line() end

  -- **Cada mostra deixa una línia al registre del sistema**: it is what a person reads when a figure on the glass looks
  -- wrong, and it is what a case of the suite measures (docs/30).
  local last = packed_count()
  local h = humidity_at(last)
  console.print(string.format('meteo: sample %d,%d%s, %d in the log%s', minute_at(last), centi_at(last),
                              h ~= nil and (',' .. h) or '', last, saved and '' or (' (' .. log_why .. ')')))
  return true
end

-- ---------------------------------------------------------------- the window, the legend and the axis

-- **La finestra triada és una preferència i viu al fitxer de l'app** (`fs.data`), com la de la `Battery`: the shape of the
-- view is in `state` and what a person chose and wants to keep is not (docs/30 §5 bis, docs/45). Viu aquí dalt perquè la
-- llegenda i l'eix la llegeixen, i una local declarada més avall seria una global per a ells dos.
local window_index = 1

-- **La finestra és la dels últims minuts fins ARA** and not up to the newest sample: a log that stopped four days ago has no
-- samples in the last 24 hours and saying so is the truth. Without a clock the last N samples are shown and the chart says so
-- instead of inventing an axis.
local function window_of(index)
  local n = packed_count()
  if n == 0 then return 1, 0, false end

  local now = minute_now()
  if now == nil then
    local first = n - Ranges[index].samples + 1
    if first < 1 then first = 1 end
    return first, n, true
  end

  local limit = now - Ranges[index].minutes
  local first = 1
  while first <= n and minute_at(first) < limit do first = first + 1 end
  return first, n, false
end

-- The line of the window: what it is and how many samples it really holds. A window shorter than the one asked for says the
-- span it really has, because «last 7 d» over six hours of log is a sentence that is not true.
local function legend_text(from, to, no_clock)
  local n = to - from + 1
  local asked = Ranges[window_index]

  if n <= 0 then return 'No samples in ' .. asked.label end
  if no_clock then return string.format('%d samples, no clock', n) end

  local span = asked.label
  if n >= 2 and minute_at(to) > minute_at(from) then
    local minutes = minute_at(to) - minute_at(from)
    if minutes < asked.minutes then
      if minutes < 120 then
        span = string.format('%d min', minutes)
      elseif minutes < 48 * 60 then
        span = string.format('%d h', math.floor(minutes / 60 + 0.5))
      else
        span = string.format('%d days', math.floor(minutes / 1440 + 0.5))
      end
    end
  end

  if n == 1 then return string.format('1 sample in %s', span) end
  return string.format('%s: %d samples', span, n)
end

-- The two ends of the time axis: hours for a day, dates for a week or a quarter. **És el que el control del sistema no pot
-- saber**, perquè només rep minuts (0.189.169).
local function axis_text(minute)
  local d = time.date(minute * 60)
  if d == nil then return '' end
  if window_index == 1 then return string.format('%02d h', d.hour) end
  return string.format('%d/%d', d.day, d.month)
end

-- **La banda de l'esquerra és la del control i es compta igual**: the widest label of the chart plus the reach of the guide
-- that leaves the frame. The bars measure themselves with this same function and this same top, perquè les dues gràfiques
-- han de tenir el mateix marge esquerre i el mateix eix del temps (docs/30, docs/90).
local function band_of(top)
  return ui.text_width(string.format('%.1f', top / 10), kSmall) + 12
end

-- ---------------------------------------------------------------- the state of the page (docs/30 § la sessió `state`)

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

-- ---------------------------------------------------------------- the window a person chose

local function read_window_choice()
  local text = fs.read(settings_path(), 32)
  if text == nil then return end
  local n = tonumber(text:match('range=(%d+)'))
  if n ~= nil and n >= 1 and n <= #Ranges then window_index = math.floor(n) end
end

local function write_window_choice()
  fs.write(settings_path(), string.format('range=%d\n', window_index))
end

-- ---------------------------------------------------------------- the page

local wid, hei = 0, 0
local column_w = 0   -- l'amplada de la columna de la dreta, que `layout()` mesura
local hum_value, hum_title, legend, chart, note, note2
local buttons = {}
local named = {}
local shape = { focus = nil }

-- The two boxes the hand paints in, measured by `layout()`.
local temp_box = { x = 0, y = 0, w = 0, h = 0 }
local bars_box = { x = 0, y = 0, w = 0, h = 0 }

-- One text centred in a box: the canvas draws from its left edge and `ui.text_width` is the only thing that knows how wide
-- the glyphs are.
local function centred(x, w, y, text, scale)
  ui.text(x + (w - ui.text_width(text, scale)) // 2, y, text, scale, INK)
end

-- **Un text de dues línies, tallat on cap** (the original's `wrap = true`, that a `ui.label` of this system does not have):
-- the break is the last word that fits in the width, and what does not fit in two lines is the label's business —it ends it
-- with three dots— (docs/30, docs/45).
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

-- ============================================================================
--  THE THERMOMETER, painted by hand in the left half (the paper project's `gauges.lua`, ported whole).
--
--  The silhouette is computed ROW BY ROW as a half width and painted as ONE object -walls and liquid, in that order-: the
--  tube and the bulb have no joint to line up and no inside to paint over something else, which is what the paper project
--  learned the hard way (a bulb that was always empty and a tube that came out cut from the circle).
-- ============================================================================
local Pad, Thick = 8, 3

-- The glass: the tube's width, the bulb's circle, and the WIDER of the two at every row. That `max` is the whole fix: with
-- the two of them added, the neck is a curve and not a joint.
local function glass_half(yy, tube_half, centre_y, radius)
  local dy = yy - centre_y
  local belly = 0
  if dy >= -radius and dy <= radius then
    belly = math.floor(math.sqrt(radius * radius - dy * dy) + 0.5)
  end
  if yy < centre_y and tube_half > belly then return tube_half end
  return belly
end

-- A row too narrow for two walls is painted SOLID: the two ends of the bulb are one or two pixels wide, and two walls there
-- would leave a white line in the middle of the curve.
local function paint_rows(cx, y0, y1, half, liquid)
  for yy = y0, y1 do
    local h = half(yy)
    if h > 0 then
      if 2 * h <= Thick then
        ui.rect(cx - h, yy, 2 * h + 1, 1, INK)
      else
        ui.rect(cx - h, yy, Thick, 1, INK)
        ui.rect(cx + h - Thick + 1, yy, Thick, 1, INK)
        if liquid and yy >= liquid then
          local inner = h - Thick
          if inner > 0 then ui.rect(cx - inner, yy, inner * 2 + 1, 1, INK) end
        end
      end
    end
  end
end

local function thermometer(box, fraction, text, title, lo, hi)
  local x, y, w, h = box.x, box.y, box.w, box.h
  local value_h, small_h = line(kTitle), line(kSmall)
  if w < 90 or (h - 2 * Pad) < value_h + small_h + 60 then return end

  -- The value and the name of the magnitude: the value in the title face and, if it does not fit, in the small one -a number
  -- that runs over its own box is worse than a small one-.
  local scale = kTitle
  if ui.text_width(text, scale) > w - 2 * Pad then scale = kSmall end
  centred(x, w, y + Pad + (scale == kTitle and 0 or (value_h - small_h)), text, scale)
  centred(x, w, y + Pad + value_h + 2, title, kSmall)

  local top = y + Pad + value_h + 2 + small_h + 8
  local bottom = y + h - Pad

  local tube_half = math.max(12, math.floor(w / 14))
  local bulb_r = math.max(tube_half + 5, math.floor(tube_half * 1.7))
  local cx = x + math.floor(w / 2)
  local bulb_y = bottom - bulb_r
  local neck = bulb_y - bulb_r
  if (neck - top) < 60 then return end

  local liquid = nil
  if fraction then
    if fraction > 1 then fraction = 1 end
    if fraction < 0 then fraction = 0 end
    liquid = bottom - math.floor(fraction * (bottom - top))
  end

  paint_rows(cx, top, bottom, function(yy) return glass_half(yy, tube_half, bulb_y, bulb_r) end, liquid)

  -- The ticks, on both sides of the TUBE and never over the bulb: a tick inside the liquid would be a hairline in the middle
  -- of the mercury.
  for k = 1, 9 do
    local ty = bottom - math.floor((bottom - top) * k / 10)
    local tl = (k == 5) and 12 or 7
    if ty >= top and ty <= neck then
      ui.rect(cx - tube_half - tl - 3, ty, tl, 1, INK)
      ui.rect(cx + tube_half + 3, ty, tl, 1, INK)
    end
  end

  -- And the two ends of the scale, each number at the end it belongs to: a column of mercury with no numbers is a decoration
  -- and not a gauge, and numbers the wrong way round are worse than none.
  if (x + w) - (cx + tube_half) > 70 then
    local right = cx + tube_half + 18
    if hi then ui.text(right, top - 4, tostring(hi), kSmall, INK) end
    if lo then ui.text(right, neck - small_h + 6, tostring(lo), kSmall, INK) end
  end
end

-- ============================================================================
--  THE BARS OF THE HUMIDITY (the paper project's `meteo_graph.bars`, ported): the same window, the same left margin and the
--  same time axis as the chart above, a fixed scale of 0 to 100 % with its three guides, and one bar per column. When there
--  are more samples than bars the bar is the MEAN of the samples that fall in it: the highest would turn ninety days of a
--  room into a comb. A sample without a humidity is a hole and not a zero.
-- ============================================================================
local function bars(box, from, to, no_clock)
  local x, y, w, h = box.x, box.y, box.w, box.h
  if w < 140 or h < 80 then return end

  -- **La banda és la del control, i també el seu sostre**: `chart_draw` no deixa que la banda passi d'un terç de l'amplada
  -- —una caixa estreta amb números amples no és un gràfic— i la de les barres s'hi ha d'ajustar igual, o les dues gràfiques
  -- comencen en dues columnes i l'eix del temps deixa de ser el mateix (docs/30, docs/90).
  local band = math.min(band_of(ChartHi), w // 3)
  local x0, x1 = x + band + 4, x + w - 6
  -- **El número de la guia de dalt penja mig full de lletra de la seva ratlla** (as the chart control writes its own three
  -- numbers), o sigui que l'aire de dalt és mig full i no un de sencer: that half line is what the bars of a short column
  -- need, and the original wrote its numbers the same way (`yy - 16`).
  local y0, y1 = y + line(kSmall) // 2 + 2, y + h - 20
  if x1 - x0 < 40 or y1 - y0 < 30 then return end

  local height, width = y1 - y0, x1 - x0
  ui.frame(x0 - 4, y0 - 2, width + 8, height + 5, INK)

  local function guide(v)
    local yy = y1 - v * height // 100
    local text = tostring(v)
    ui.rect(x0, yy, width + 1, 1, INK)
    ui.text(x0 - 12 - ui.text_width(text, kSmall), yy - line(kSmall) // 2, text, kSmall, INK)
  end
  guide(100)
  guide(50)
  guide(0)

  local n = to - from + 1
  if n <= 0 then return end

  local t0, t1 = minute_at(from), minute_at(to)
  local span = (not no_clock and t1 > t0) and (t1 - t0) or 0

  -- Three pixels is the narrowest a bar can be and still read as a bar: with more samples than that, the samples of one bar
  -- are averaged.
  local slots = math.min(n, math.max(1, width // 3))
  local sum, count = {}, {}
  for k = from, to do
    local v = humidity_at(k)
    if v ~= nil then
      local s
      if span > 0 then
        s = math.floor((minute_at(k) - t0) * slots / span)
      else
        s = math.floor((k - from) * slots / n)
      end
      if s < 0 then s = 0 end
      if s > slots - 1 then s = slots - 1 end
      sum[s] = (sum[s] or 0) + v
      count[s] = (count[s] or 0) + 1
    end
  end

  -- A bar wider than a few tens of pixels is a wall: with one or two samples in the window the pitch is the whole width, and
  -- a single wall reads as a fill.
  local pitch = width / slots
  local bar_w = math.max(1, math.floor(pitch) - 1)
  if bar_w > 40 then bar_w = 40 end

  for s = 0, slots - 1 do
    if count[s] then
      local v = math.floor(sum[s] / count[s] + 0.5)
      local bh = v * height // 100
      -- **Un zero conegut és una barra d'un píxel** i no un forat: la diferència entre «la sala era al 0 %» i «ningú no ho
      -- va mesurar» és tot el sentit del byte 255.
      if bh < 1 then bh = 1 end
      ui.rect(x0 + math.floor(s * pitch), y1 - bh + 1, bar_w, bh, INK)
    end
  end
end

-- ---------------------------------------------------------------- the page, built once and laid out on every change of room

-- **`set_window` es diu des del botó que es construeix aquí i es defineix després**, que és on cau la pàgina sencera: un nom
-- que encara no és local en el moment de compilar aquesta funció seria una global i el botó no faria res.
local set_window

local function build()
  local why = nil
  wid, hei = ui.caps().width, ui.caps().height
  named = {}

  hum_value, why = ui.label(2, 2)
  if hum_value == nil then
    console.print('meteo: no number for the humidity (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  hum_value.text = '--%'
  hum_value.scale = kBig
  hum_value.align = 'center'

  hum_title, why = ui.label(2, 2)
  if hum_title == nil then
    console.print('meteo: no name for the humidity (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  hum_title.text = 'Humidity'
  hum_title.scale = kSmall
  hum_title.align = 'center'

  legend, why = ui.label(2, 2)
  if legend == nil then
    console.print('meteo: no line for the window (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  legend.text = ''
  legend.scale = kSmall

  chart, why = ui.chart(2, 2)
  if chart == nil then
    console.print('meteo: no chart (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  -- **Els números de la banda, a la mida del text de l'original** (0.189.169): one decimal -the value arrives multiplied by
  -- ten, which is what `decimals` means- and the paper's own small face, so the drawing of the two charts is the same size.
  chart.scale = kSmall
  chart.decimals = 1
  chart.suffix = ''
  chart.gap = 180
  chart.min = ChartLo
  chart.max = ChartHi

  note, why = ui.label(2, 2)
  if note == nil then
    console.print('meteo: no foot (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  note.text = ''
  note.scale = kSmall

  note2, why = ui.label(2, 2)
  if note2 == nil then
    console.print('meteo: no second line of the foot (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  note2.text = ''
  note2.scale = kSmall

  buttons = {}
  for index, one in ipairs(Ranges) do
    local button = nil
    button, why = ui.button(2, 2)
    if button == nil then
      console.print('meteo: no button for ' .. one.label .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
      return false
    end
    button.title = one.label
    button.primary = (index == window_index)
    local name = one.id
    button.on_focus = function(self, on)
      if on then
        shape.focus = name
        save_state(shape)
      end
    end
    button.on_click = function()
      set_window(index)
    end
    named[name] = button
    buttons[#buttons + 1] = button
  end

  if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end
  return true
end

-- **El mínim de les barres i el de la corba**, en píxels de línia de la cara del sistema: les barres necessiten la guia de
-- dalt amb el seu número, trenta píxels de paper i l'aire del zero; la corba, les seves dues ratlles de números i vint-i-quatre
-- píxels de paper entre elles.
local kHumBarsMin = line(kSmall) // 2 + 52
local kChartMin = 2 * line(kSmall) + 32

-- **El repartiment de la columna de la dreta, que és el que l'original no pot donar fet**: the paper spends 82 + 33 + 140 + 33
-- + 20 = 308 pixels of the row and leaves the curve twenty-seven —those are the lines of ITS screen—, o sigui que aquí es
-- reparteix el que queda: **les barres es queden el 27 % com a l'original** (140 de 519) amb el mínim que necessiten per
-- dibuixar-se, i la corba la resta. I quan el que queda no hi arriba, **el número de la humitat baixa a la cara del títol**,
-- que és la llei que el `gauges.lua` de l'original ja va escriure: un número gros que no deixa lloc al dibuix és un número que
-- s'ha de fer petit (docs/45).
local function bars_and_chart(row_h, big_h, small_h)
  -- **Una columna que no hi cap es reparteix igual** i el que en surt és el mínim que cada dibuix accepta: una mida negativa
  -- no és una pàgina petita, és un control que el sistema no pot col·locar.
  local room = row_h - 10 - (big_h + 2 * small_h + 26)
  if room < 100 then room = 100 end
  local bars_h = room * 27 // 100
  if bars_h < kHumBarsMin then bars_h = kHumBarsMin end
  local chart_h = room - bars_h
  if chart_h < kChartMin then
    chart_h = kChartMin
    bars_h = room - chart_h
    if bars_h < kHumBarsMin then bars_h = kHumBarsMin end
  end
  if bars_h < 1 then bars_h = 1 end
  if chart_h < 1 then chart_h = 1 end
  return bars_h, chart_h
end

local function layout()
  local inner_w = wid - 2 * MARGIN
  local note_top = hei - MARGIN - NoteH
  local ranges_top = note_top - GAP - RangeH
  local row_h = ranges_top - GAP - MARGIN
  if row_h < 120 then row_h = 120 end

  local left_w = (inner_w - GAP) * TempShare // (TempShare + RightShare)
  local right_w = inner_w - GAP - left_w
  local rx = MARGIN + left_w + GAP

  temp_box.x, temp_box.y, temp_box.w, temp_box.h = MARGIN, MARGIN, left_w, row_h

  local small_h = line(kSmall)
  local big_h, bars_h, chart_h = line(kBig), 0, 0
  bars_h, chart_h = bars_and_chart(row_h, big_h, small_h)
  if chart_h < kChartMin then
    big_h = line(kTitle)
    bars_h, chart_h = bars_and_chart(row_h, big_h, small_h)
  end
  hum_value.scale = (big_h >= line(kBig)) and kBig or kTitle

  local y = MARGIN
  hum_value:resize(right_w, big_h)
  hum_value:place(rx, y)
  y = y + big_h + 2

  hum_title:resize(right_w, small_h)
  hum_title:place(rx, y)
  y = y + small_h + 2

  -- **Les barres viuen just sota el número que són**, and the curve takes the rest.
  bars_box.x, bars_box.y, bars_box.w, bars_box.h = rx, y, right_w, bars_h
  y = y + bars_h + 2

  legend:resize(right_w, small_h)
  legend:place(rx, y)
  -- **El gràfic comença vint píxels sota la línia de la finestra** i deixa deu píxels a baix: the owner saw the top of a chart
  -- over that line in the paper project, and the air is what separates them (docs/30).
  y = y + small_h + 20

  chart:resize(right_w, chart_h)
  chart:place(rx, y)
  column_w = right_w

  local count = #buttons
  if count > 0 then
    local each = (inner_w - (count - 1) * 14) // count
    for index, one in ipairs(buttons) do
      one:resize(each, RangeH)
      one:place(MARGIN + (index - 1) * (each + 14), ranges_top)
    end
  end

  note:resize(inner_w, small_h)
  note:place(MARGIN, note_top)
  note2:resize(inner_w, small_h)
  note2:place(MARGIN, note_top + small_h)
end

-- **El que el peu diu**, in the order of what matters most: what could not be done, then what cannot be read, then what was
-- read. The two lines are the original's one wrapped sentence, cut where it turns.
local function refresh()
  local from, to, no_clock = window_of(window_index)
  legend.text = legend_text(from, to, no_clock)
  -- **La línia de la finestra baixa de cara abans que perdre cap paraula** (the law the original's `gauges.lua` wrote for the
  -- thermometer): «40 min: 2 samples» no cap a la columna amb la cara de la pàgina —la del disc és més ampla que el mapa de
  -- bits del projecte del paper— i el que diu la finestra és el que una persona llegeix primer, o sigui que el text es fa
  -- petit i no es talla amb tres punts (docs/30, docs/45).
  local scale = kSmall
  while scale > 1 and column_w > 0 and ui.text_width(legend.text, scale) > column_w do
    scale = scale - 1
  end
  legend.scale = scale

  -- **Els punts del gràfic, aprimats a quaranta-vuit**: one of every so many samples and ALWAYS the last one, which is the
  -- number the control draws. A thinned window cannot tell a big step from a hole and says so with `gap = 0` (the thinning
  -- already made those two hours apart); the 24 h window fits whole and keeps the rule of the three hours (docs/30).
  local values, times = {}, {}
  local n = to - from + 1
  if n > 0 then
    local thinned = n > MostPoints
    local step = thinned and math.ceil(n / (MostPoints - 1)) or 1

    local k = from
    while k <= to do
      values[#values + 1] = math.floor(centi_at(k) / 10)
      times[#times + 1] = no_clock and -1 or minute_at(k)
      if k ~= to and (k + step) > to then
        values[#values + 1] = math.floor(centi_at(to) / 10)
        times[#times + 1] = no_clock and -1 or minute_at(to)
        break
      end
      k = k + step
    end
    chart.gap = thinned and 0 or 180
    chart.left = no_clock and '' or axis_text(minute_at(from))
    chart.right = no_clock and '' or axis_text(minute_at(to))
  else
    chart.gap = 180
    chart.left = ''
    chart.right = ''
  end

  -- **L'escala del gràfic és la de la finestra** (the owner, 20/10: *«la gráfica que sigui usant els valors que es tinguin en
  -- aquell rang de temps»*): `chart_draw` takes the two ends of the paper from the samples it is given and only clamps them
  -- into `min`/`max`, so the two numbers of the band are the window's and two degrees of a quiet week are a curve and not a
  -- flat line in the middle of an instrument's scale. **El termòmetre sí que guarda la seva escala fixa**: un instrument no
  -- canvia d'escala perquè la setmana sigui càlida.
  chart.values = values
  chart.times = times

  local hum = '--%'
  if now_humidity ~= nil then hum = string.format('%d%%', math.floor(now_humidity / 100 + 0.5)) end
  hum_value.text = hum

  -- **El peu és una sola frase, com a l'original**, i les dues línies es fan aquí perquè el sistema no en té de `wrap`: the
  -- sentence is the paper project's own —«Read at 12:34 (5). Redraw on change; log every 30 min.»— and a message that could not
  -- be done takes its place, with the same two lines (docs/30, docs/45).
  local foot = nil
  if log_why ~= '' then
    foot = log_why
  elseif alarm_why ~= '' then
    foot = alarm_why
  elseif no_sensor ~= '' then
    foot = no_sensor
  elseif clock_why ~= '' then
    foot = clock_why
  else
    local when = ''
    if read_hour ~= nil then when = string.format(' at %02d:%02d', read_hour, read_minute) end
    foot = string.format('Read%s (%d). Redraw on change; log every %d min.', when, reads, SampleMinutes)
  end
  note.text, note2.text = two_lines(foot, wid - 2 * MARGIN, kSmall)
end

-- **Es pinten a mà els dos instruments i el control es pinta sol**: the page is cleared first -a repaint that did not clear
-- would leave the mercury of the reading before under the new one- and then the system composes it (docs/30, docs/50).
local function paint_page(mode)
  refresh()
  ui.clear(PAPER)

  if now_temp ~= nil then
    local celsius = now_temp / 100
    thermometer(temp_box, (celsius - TempLo) / (TempHi - TempLo), string.format('%.1f %sC', celsius, DEGREE), 'Temperature',
                TempLo, TempHi)
  else
    thermometer(temp_box, nil, '--', 'Temperature', TempLo, TempHi)
  end

  local from, to, no_clock = window_of(window_index)
  bars(bars_box, from, to, no_clock)
  ui.present(mode or kPageMode)
end

-- **Una finestra nova és una pàgina nova**: the scale, the axis and the whole curve change, and the owner's page repaints
-- itself whole for it (docs/30, docs/50).
set_window = function(index)
  if index < 1 or index > #Ranges then return end
  window_index = index
  for i = 1, #buttons do
    buttons[i].primary = (i == index)
  end
  write_window_choice()
  save_state(shape)
  paint_page(kWindowMode)
end

-- ---------------------------------------------------------------- the reading of the alarm, before anything else is built

-- **El que ha sonat es fa dins del ganxo i no al bucle** (0.189.145): a window calls `on_alarm` and destroys the machine right
-- after, so the sensor, the sample and the line of the log are done here and cannot wait. In a window nothing of the page
-- above has run, so the log is read here the first time.
local due = false
local log_open_in_the_hook = false

function on_alarm(id, payload, meta)
  local mine = (payload == 'sample') or (meta ~= nil and meta.missed ~= nil and meta.missed > 0)
  if not mine then return end
  if not log_open_in_the_hook then
    log_read = load_log()
    log_open_in_the_hook = true
  end
  read_sensor()
  take_sample()
  due = true
end

-- **Una finestra només porta la crida de l'alarma** (0.189.145): the system loads the entry to open the app and calls the hook
-- right away, and without this door every window would build a page nobody sees and arm an alarm the system would answer
-- with another window, for ever.
if not console.is_foreground() then return end

-- ---------------------------------------------------------------- the page the person sees

read_window_choice()

local was = load_state()
if was ~= nil and was.focus ~= nil then
  -- **Un nom que no és cap dels botons no és un focus, és una pàgina sense anell**: the name is valued before it is used
  -- (docs/30, docs/45 §5 bis).
  for _, one in ipairs(Ranges) do
    if one.id == was.focus then shape.focus = was.focus end
  end
end

if not build() then return end
layout()

log_read = load_log()
log_open_in_the_hook = true

-- **El comiat de la pàgina desa la forma**, i les tres portes van abans del bucle: a chunk that never ends registers nothing
-- that comes after it (docs/30, docs/45).
function on_pause() save_state(shape) end
function on_save() save_state(shape) end
function on_close() save_state(shape) end

console.print(string.format('meteo: %d sample(s) in the log, window %s', packed_count(), Ranges[window_index].label))

-- **Les promeses velles d'aquesta app es donen per acabades abans d'armar-ne una de nova**: the queue of the system survives a
-- restart and a copy of the board, and an app that comes back with six of its own cannot arm another one (docs/30).
-- **I només les seves**: `alarm.list()` és la cua del sistema —la de la `battery` també hi és— i anular-ne una que no és
-- d'aquesta app només deixa una línia d'avís al registre (`meteo cannot cancel #1: it is not one of its alarms`) i la promesa
-- de l'altra app intacta, o sigui que el filtre és el camp `app` i no el `pcall` (0.189.157, docs/30).
do
  local ok, mine = pcall(alarm.list)
  if ok and type(mine) == 'table' then
    for _, one in ipairs(mine) do
      if type(one) == 'table' and one.id ~= nil and one.app == app.id then pcall(alarm.cancel, one.id) end
    end
  end
end

-- **Una mostra en obrir-se si toca**, que és el que fa que la pàgina tingui el punt d'aquesta mitja hora abans que no passi
-- l'alarma, com fa la `Battery`.
read_sensor()
take_sample()

-- `repeat` is a reserved word of Lua: the field of the alarm is written with the brackets (docs/30, docs/60).
local armed, why = pcall(alarm.at, { in_ms = EveryMs, payload = 'sample',
                                     ["repeat"] = math.floor(EveryMs / 1000), persist = true })
if not armed then
  alarm_why = 'No alarm: it only reads when the app is opened'
  console.print('meteo: ' .. alarm_why .. ' (' .. tostring(why) .. ')')
end
save_state(shape)
paint_page()

-- **El que el vidre ensenya, com una sola cadena**: it is what decides whether a reading asks for a pass, and the hour and the
-- number of readings are not in it on purpose (they change at every reading) (docs/30).
local function shown_state()
  return table.concat({ tostring(now_temp), tostring(now_humidity), no_sensor, clock_why, log_why, alarm_why,
                        tostring(packed_count()), tostring(window_index) }, '|')
end

local plotted = shown_state()

while true do
  -- **Un canvi de mida torna a fer la pàgina sencera**: the system makes a new view for the whole glass or for the band of the
  -- keyboard, and every canvas of the page comes back dead, so the page is built again in the box it has now (docs/30).
  local c = ui.caps()
  if c.width ~= wid or c.height ~= hei then
    wid, hei = c.width, c.height
    if build() then
      local again = load_state()
      if again ~= nil and again.focus ~= nil then shape.focus = again.focus end
      if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end
      layout()
      paint_page()
      plotted = shown_state()
    end
  end

  -- **L'alarma ha deixat una lectura feta**: the hook already wrote it, and the page only repaints if the glass would say
  -- something else (docs/30).
  if due then
    due = false
    if shown_state() ~= plotted then
      paint_page()
      plotted = shown_state()
    end
  end

  -- **Un toc al paper llegeix i pren la mostra d'ara si toca**: a gesture no control has taken is the page's, and the whole
  -- screen is the button, as it was in the paper project (docs/30, docs/90).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'tap' then
      read_sensor()
      take_sample()
      if shown_state() ~= plotted then
        paint_page()
        plotted = shown_state()
      end
    end
    gesture = touch.gesture()
  end

  sys.delay(150):await()
end
