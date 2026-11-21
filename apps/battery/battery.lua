-- battery: **la bateria de les últimes hores, en un gràfic** (0.189.144, portada de `data/app/Battery` del projecte del
-- paper). The paper project wrote it with three decisions that this port keeps whole (its README §10.4): **the sample is
-- taken by an ALARM** —the only way the system calls an app that is not in front—, **the data is in a FILE** —the RAM goes
-- off— and **no prediction is made**: the page says what the battery did and not what it will do.
--
-- El que canvia respecte de l'original és el que el sistema ja té: la pàgina és un `ui.label` i tres `ui.button` i el
-- gràfic és **un control del sistema** (`ui.chart`, nou aquí perquè el gràfic del paper era un dibuix de l'app), i el
-- registre viu a la carpeta de l'app (`fs.data`) en lloc d'una unitat triada a mà.
--
-- **Les portes que fa servir**: `power.battery` (el número d'ara), `time.date` (l'hora de la mostra), `fs.data`/`fs.read`/
-- `fs.write` (el registre i la preferència), `alarm.at` (una mostra cada deu minuts), `sys.session` (la forma de la vista) i
-- `ui.*` (la pàgina). Cap d'elles no és nova.
--
-- **El que és una preferència i el que és forma**: la finestra triada (1, 6 o 24 hores) és una decisió de la persona i va al
-- fitxer de l'app; el cercle del dial, que és on era la persona, va a la sessió `state` (docs/30, docs/45 §5 bis).

local caps = sys.caps()
if not caps.display then
  console.print('battery: this board has no glass')
  return
end

local kLayout = 'v1'          -- puja quan un control canvia de nom o la pàgina canvia de forma
-- **La pàgina es rentja amb setze nivells i sense parpelleig** (0.189.144): el gràfic és una línia que es torna a fer
-- sencera cada cop, i un `du` deixa el fantasma de la línia d'abans —la mateixa lliçó que el projecte del paper va apuntar
-- del seu gràfic: el dibuix es repinta amb setze grisos i no amb la passada barata— (docs/50, docs/90).
local kPageMode = 'gld16'

local MARGIN = 20
local GAP = 14
local BIG_H = 96              -- el percentatge, que és el número de la pàgina
local DETAIL_H = 32           -- els volts i el cable, a sota
local LEGEND_H = 34           -- la frase de la finestra
local NOTE_H = 66             -- dues ratlles: el peu i el que no s'ha pogut fer
local ROW_H = 63              -- la fila dels tres botons

-- **Una mostra cada deu minuts**, que és el que el projecte del paper va triar (docs/60): the battery of a board moves
-- about a percent every half hour, and a sample a minute would be a log of paper.
local kSampleMs = 10 * 60 * 1000
local kSampleMinutes = 10

-- **Vint-i-quatre hores a deu minuts són cent quaranta-quatre mostres**, i el gràfic en dibuixa quaranta-vuit: el registre
-- guarda el dia sencer i la finestra que s'ensenya se'n queda una de cada tres quan cal. El sostre és el del registre i no
-- el del control, perquè una finestra de vint-i-quatre hores amb quaranta-vuit mostres és mitja hora per mostra.
local kMostSamples = 144
local kChartSamples = 48

-- Les tres finestres, dites en hores, i el nom de cada botó.
local Ranges = { 1, 6, 24 }

-- **El llindar d'aquesta app és nou per cent** i no el del sistema: el que apaga la placa és un altre número (docs/60), i
-- l'avis d'aquí és el d'una pàgina que mira el que ha passat, no el d'un sistema que s'ha de protegir.
local kLowPct = 9

local kLog = 'samples.txt'    -- a `fs.data()`, que és la carpeta d'aquesta app
local kPrefs = 'settings.txt'

-- La finestra que s'ensenya, per índex de `Ranges`; la sisena hora és la de sortida, com a l'original.
local range = 2

-- El registre, de la mostra més vella a la més nova: `{t=minuts del dia o -1, p=percentatge}`. Dues llistes de taules
-- serien el doble de memòria per als mateixos números, i una mostra és una taula de dues claus.
local samples = {}

local trouble = ''            -- el que no s'ha pogut llegir o desar
local armed = nil             -- l'alarma que porta les mostres

local shape = { focus = nil }
local session = nil
local named = {}              -- el nom de cada botó, per la sessió

local wid, hei = 0, 0
local big, detail, chart, legend, note, note2
local buttons = {}

-- ---------------------------------------------------------------- la carpeta de l'app

local function my_folder()
  local ok, folder = pcall(fs.data)
  if not ok or type(folder) ~= 'string' or folder == '' then return nil end
  pcall(fs.mkdir, folder)     -- ja hi és gairebé sempre, i un error d'aquí no és res
  return folder
end

local function my_file(name)
  local folder = my_folder()
  if folder == nil then return nil end
  return folder .. '/' .. name
end

-- ---------------------------------------------------------------- la sessió `state`

local function the_state()
  if session == nil then
    session = sys.session('state', 1024)
  end
  return session
end

local function save_state()
  local block = the_state()
  if block == nil then return end
  local lines = { kLayout }
  if shape.focus ~= nil then lines[#lines + 1] = 'focus=' .. shape.focus end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
end

-- **El bloc es llegeix sencer i s'acaba a la primera marca de zero** (0.189.52): the block is a kilobyte and what was
-- written is shorter, so what comes after is the zero the kernel made it with —and the zero of the page that was longer
-- before—, and a line with zeros behind it is not a value (docs/30, docs/45 §5 bis).
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

-- ---------------------------------------------------------------- la preferència

local function read_range()
  local path = my_file(kPrefs)
  if path == nil then return end
  local ok, text = pcall(fs.read, path)
  if not ok or type(text) ~= 'string' then return end
  local wanted = tonumber(text:match('range=(%d+)') or '')
  if wanted ~= nil and Ranges[wanted] ~= nil then range = wanted end
end

local function write_range()
  local path = my_file(kPrefs)
  if path == nil then return end
  -- **`fs.write` torna els bytes i no un booleà** (docs/30): a `pcall` li és igual que la porta respongui `nil, error`, i
  -- una preferència que no s'ha pogut desar no és cap drama; el que no es pot fer és dir que sí.
  fs.write(path, string.format('range=%d\n', range))
end

-- ---------------------------------------------------------------- el registre

-- **Una mostra és una línia `minuts,percentatge`**, i el minut és **-1 quan aquesta placa no té rellotge**: the paper
-- project wrote the same shape and this is the one a person can read with `cat` if the app ever goes wrong (docs/30).
local function read_log()
  samples = {}
  local path = my_file(kLog)
  if path == nil then return end
  -- **Un fitxer que no hi és no és un error**: és la primera vegada d'aquest registre (docs/30).
  local text, why = fs.read(path)
  if type(text) ~= 'string' then
    local code = type(why) == 'table' and why.code or nil
    if code ~= nil and code ~= 'E_NOENT' and code ~= 'E_IO' then
      trouble = 'The log could not be read (' .. tostring(code) .. ').'
    end
    return
  end
  for line in text:gmatch('[^\n]+') do
    local t, p = line:match('^(%-?%d+),(%-?%d+)$')
    if t ~= nil then
      local minutes, percent = tonumber(t), tonumber(p)
      if minutes ~= nil and percent ~= nil and percent >= 0 and percent <= 100 then
        samples[#samples + 1] = { t = minutes, p = percent }
      end
    end
  end
  while #samples > kMostSamples do table.remove(samples, 1) end
end

local function write_log()
  local path = my_file(kLog)
  if path == nil then
    trouble = 'This app has no folder of its own, so the log is in this page only.'
    return false
  end
  local text = ''
  for _, one in ipairs(samples) do
    text = text .. one.t .. ',' .. one.p .. '\n'
  end
  -- **La resposta és el que diu si s'ha escrit**: `fs.write` torna els bytes o `nil, error`, i un `pcall` sol no ho veu
  -- (docs/30). Una pàgina que diu que ha desat quan no ho ha fet és pitjor que una que no desa.
  local written, why = fs.write(path, text)
  if type(written) ~= 'number' then
    trouble = 'The log could not be saved: ' .. tostring(why and why.code or why or 'no room')
    return false
  end
  trouble = ''
  return true
end

-- ---------------------------------------------------------------- el que la placa diu d'ara

local function now_of_the_board()
  local ok, battery = pcall(power.battery)
  if not ok or type(battery) ~= 'table' then return nil end
  if type(battery.pct) ~= 'number' or battery.pct < 0 then return nil end
  -- **L'hora de la mostra és el minut del dia**, i **-1 si el rellotge no respon**: una placa sense hora no pot ordenar el
  -- seu registre i la pàgina ho ha de dir en lloc d'inventar-se un eix (docs/60).
  local t = -1
  local today = time.date()
  if today ~= nil then t = today.hour * 60 + today.min end
  -- **I el que el comptador de coulombs sap de més** (0.190.155, l'amo: *«Mira els exemples de batería (es el hardware que tenim?)
  -- ... Si ens serveixen, afegeix la informació a l'app battery»*): the BQ27220 of this board counts coulombs, o sigui que també diu
  -- el corrent que entra o surt de la cel·la i la càrrega que hi queda. Una placa amb un divisor en un ADC no té aquests camps i
  -- la pàgina no els troba (`nil`), que és el que ha de fer una pàgina que no sap si el seu número és cert (docs/30, docs/50).
  return t, battery.pct, battery.mv, battery.charging, battery.ma, battery.mah, battery.mah_full
end

-- **El rellotge que porta les mostres** (0.190.153, l'amo: *«Afegeix l'informació a l'app Battery»*). The alarm that takes a
-- sample every ten minutes is worth exactly what the clock that keeps it is worth: a board whose chip is holding the hour
-- samples with the board powered off, and one whose chip lost its cell —or has none— only samples while somebody is using it.
-- `time.caps()` ja ho deia i no ho mirava ningú, o sigui que el gràfic podia quedar pla i semblar una bateria quieta.
-- **El model el dona la placa** (`chip`: "PCF8563" aquí, "BM8563" al Paper, buit on no n'hi ha) i **l'estat el xip** (`cell`:
-- "ok", "lost", "silent" o "none"), que és la diferència entre canviar una pila i buscar un cable (docs/30, docs/50, docs/60).
local function clock_of_the_board()
  local ok, one = pcall(time.caps)
  if not ok or type(one) ~= 'table' then return nil end
  return one
end

-- **Una mostra per minut com a molt**: dues lectures dins del mateix minut són la mateixa mostra, i el rellotge és el que
-- ho diu. Sense rellotge, una de cada crida, que és el que l'original feia.
local function record()
  local t, percent = now_of_the_board()
  if percent == nil then return false end
  local last = samples[#samples]
  if last ~= nil and t >= 0 and last.t == t then return false end
  samples[#samples + 1] = { t = t, p = percent }
  while #samples > kMostSamples do table.remove(samples, 1) end
  trouble = ''
  write_log()
  -- **Cada mostra deixa una línia al registre del sistema**: it is what a person reads when the page says something that does
  -- not look right, and what a case of the suite measures (docs/30, docs/85).
  console.print(string.format('battery: sample %d%% at %s', percent,
                              (t >= 0) and string.format('%02d:%02d', t // 60, t % 60) or 'no clock'))
  return true
end

-- ---------------------------------------------------------------- les finestres

-- **La finestra triada, en mostres**: the ones of the last N hours when the clock is known, and the last N times ten
-- minutes when it is not —a board with no clock still knows how often the alarm comes—.
local function window_of(index)
  local n = #samples
  if n == 0 then return 0, -1 end
  local last = samples[n]
  local first = n
  if last.t >= 0 then
    local limit = last.t - Ranges[index] * 60
    while first > 1 and samples[first - 1].t >= limit do first = first - 1 end
  else
    first = math.max(1, n - Ranges[index] * 60 / kSampleMinutes + 1)
  end
  return first, n
end

-- **Una mostra de cada tantes quan la finestra no en cap**: the chart holds forty-eight and a day of samples is a hundred
-- and forty-four, so the window is thinned evenly and the last sample is always the last one. A chart that dropped the
-- newest sample would say the battery stopped an hour ago (docs/90).
local function thin(first, last)
  local count = last - first + 1
  if count <= kChartSamples then return first, last, 1 end
  return first, last, math.ceil(count / kChartSamples)
end

-- **Els minuts han de pujar perquè el registre sigui una línia del temps**: a clock that was set from the network or a page
-- that crossed the hour of summer can put a sample behind the one before it, and a chart that draws that as a timeline is a
-- chart that says the battery went backwards. From the first sample that breaks it the window has **no clock** and the page
-- says so, which is the rule the paper project followed with a sample of its own (docs/60).
local function timeline(first, last)
  local before = nil
  for i = first, last do
    local t = samples[i].t
    if t < 0 then return false end
    if before ~= nil and t < before then return false end
    before = t
  end
  return true
end

local function trend_of(first, last)
  if last - first < 1 then return nil end
  local a, b = samples[first], samples[last]
  local minutes = 0
  if a.t >= 0 and b.t >= 0 then
    minutes = b.t - a.t
  else
    minutes = (last - first) * kSampleMinutes
  end
  if minutes < 20 then return nil end
  return (b.p - a.p) * 60 / minutes
end

-- **La frase de la finestra diu la finestra de debò**: with a log of two hours the page says "Last 2 h" and not "Last 6 h",
-- perquè un sostre que sembla una mesura és una mentida (the rule the paper project wrote for this line).
local function legend_text(first, last)
  local n = last - first + 1
  if n <= 0 then return 'No samples yet.' end
  -- **Una mostra no és una tendència**: one sample says where the battery is and nothing about where it goes, and a page
  -- that wrote "steady" would be inventing a measurement (the rule the paper project wrote for this line).
  if n < 2 then return 'One sample so far.' end
  local a, b = samples[first], samples[last]
  local with_clock = timeline(first, last)
  local trend = trend_of(first, last)
  local how = 'steady'
  if trend ~= nil and trend > 0.2 then how = string.format('+%.1f %%/h', trend) end
  if trend ~= nil and trend < -0.2 then how = string.format('%.1f %%/h', trend) end
  if not with_clock then return string.format('%d samples, no clock, %s', n, how) end
  -- **La finestra que es diu és la que hi ha**, no la que s'ha triat: a log of two hours says "Last 2 h" and not "Last 6 h"
  -- (the paper project's rule for this line).
  local minutes = b.t - a.t
  local window
  if minutes < Ranges[range] * 60 then
    if minutes < 60 then window = string.format('Last %d min', minutes) else
      window = string.format('Last %d h', math.floor(minutes / 60))
    end
  else
    window = string.format('Last %d h', Ranges[range])
  end
  return string.format('%s: %d samples, %s', window, n, how)
end

-- ---------------------------------------------------------------- la pàgina

local function collect()
  local first, last, every = thin(window_of(range))
  -- **Sense línia del temps el gràfic no en dibuixa cap**: the samples go one after the other and the axis says "no clock",
  -- which is what the control does when its minutes are negative (docs/30).
  local ordered = timeline(first, last)
  local values, times = {}, {}
  for i = first, last, every do
    values[#values + 1] = samples[i].p
    times[#times + 1] = ordered and samples[i].t or -1
  end
  -- **L'última mostra hi és sempre**: the thinning must not lose where the history arrives.
  if #values > 0 and (last - first) % every ~= 0 then
    values[#values + 1] = samples[last].p
    times[#times + 1] = ordered and samples[last].t or -1
  end
  return values, times
end

local function refresh()
  local _, percent, mv, charging, ma, mah, mah_full = now_of_the_board()
  if big ~= nil then
    big.text = (percent ~= nil) and string.format('%d%%', percent) or '--'
  end
  if detail ~= nil then
    local words = ''
    if mv ~= nil and mv > 0 then words = string.format('%.3f V', mv / 1000) end
    -- **El corrent, amb el seu signe** (0.190.155): el que entra i el que surt són el mateix número amb dos signes, i és el que
    -- explica el pendent del gràfic. **Les capacitats només hi caben en un vidre ample**: en un de petit el que es perd seria el
    -- cable, i el cable és el que una persona mira quan la placa no carrega (docs/30, docs/92).
    if ma ~= nil then words = words .. string.format('   %+d mA', ma) end
    local wide = (wid <= 0) or (wid >= 400)
    if wide and mah ~= nil and mah_full ~= nil and mah_full > 0 then
      words = words .. string.format('   %d/%d mAh', mah, mah_full)
    end
    if charging then words = words .. '   plugged in' end
    detail.text = words
  end
  if chart ~= nil then
    local values, times = collect()
    chart.values = values
    chart.times = times
    chart.suffix = '%'
    -- **El rang físic d'una bateria és zero a cent**, i el gràfic no en surt ni amb l'aire d'un punt: una bateria plena
    -- encara diu 100 (docs/90).
    chart.min = 0
    chart.max = 100
  end
  if legend ~= nil then
    local first, last = window_of(range)
    legend.text = legend_text(first, last)
  end
  if note ~= nil then
    if trouble ~= '' then
      note.text = trouble
      if note2 ~= nil then note2.text = '' end
    else
      -- **La frase del peu diu qui compta les mostres** (0.190.153): el xip amb pila les porta amb la placa apagada, i el que
      -- no la té —o l'ha perduda— només mentre la placa és desperta. És el que explica un gràfic que s'acaba quan s'apaga.
      local clock = clock_of_the_board()
      local chip = (clock ~= nil and type(clock.chip) == 'string') and clock.chip or ''
      local cell = (clock ~= nil and type(clock.cell) == 'string') and clock.cell or 'none'
      if cell == 'lost' or cell == 'silent' or cell == 'none' then
        if cell == 'none' then
          note.text = 'This board has no clock chip:'
        elseif cell == 'lost' then
          note.text = ((chip ~= '') and chip or 'The clock chip') .. ' lost its cell:'
        else
          note.text = ((chip ~= '') and chip or 'The clock chip') .. ' does not answer:'
        end
        if note2 ~= nil then note2.text = 'the samples stop while the board is off.' end
      else
        note.text = (chip ~= '') and string.format('A sample every ten minutes, on the %s.', chip)
                                  or 'A sample every ten minutes,'
        if note2 ~= nil then note2.text = 'A tap on the paper takes one now.' end
      end
    end
  end
  -- **La finestra triada porta el marc gruixut**, que és el senyal que aquest sistema té per a «aquesta és la triada»: the
  -- primary frame is three pixels and the others one, and the ring of the dial is still the person's (docs/30, docs/92).
  for index, one in ipairs(buttons) do
    one.primary = (index == range) and true or false
  end
end

-- **Una finestra nova es rentja sencera i una mostra nova no**: a range change draws a whole other line in the same box, and
-- the sixteen-level pass of the paper project leaves the ghost of the one before it —which is what the emulator showed on
-- the third tap—; a new sample moves the end of the line and the cheap wash is enough. The two blinks of a `gc16` are paid
-- once, when a person asks for another window, and never on the ten-minute sample (docs/50, docs/90).
local function paint_page(mode)
  refresh()
  ui.present(mode or kPageMode)
end

local function layout()
  local width = wid - 2 * MARGIN
  if big ~= nil then
    big:resize(width, BIG_H)
    big:place(MARGIN, MARGIN)
  end
  local y = MARGIN + BIG_H + 2
  if detail ~= nil then
    detail:resize(width, DETAIL_H)
    detail:place(MARGIN, y)
  end
  y = y + DETAIL_H + GAP

  -- **El gràfic s'emporta el que queda**: the three buttons and the two lines are fixed, and the chart is the elastic one
  -- (the rule the paper project wrote for this page, and the reason its own chart filled the middle).
  local bottom = hei - MARGIN - ROW_H
  local note_top = bottom - GAP - NOTE_H
  local legend_top = note_top - GAP - LEGEND_H
  local chart_h = legend_top - GAP - y
  if chart ~= nil then
    chart:resize(width, math.max(80, chart_h))
    chart:place(MARGIN, y)
  end
  if legend ~= nil then
    legend:resize(width, LEGEND_H)
    legend:place(MARGIN, legend_top)
  end
  if note ~= nil then
    note:resize(width, NOTE_H / 2)
    note:place(MARGIN, note_top)
  end
  if note2 ~= nil then
    note2:resize(width, NOTE_H / 2)
    note2:place(MARGIN, note_top + NOTE_H / 2)
  end

  -- **Els tres botons es reparteixen l'amplada a parts iguals**, i el que té la finestra triada es queda marcat.
  local count = #buttons
  if count > 0 then
    local each = math.floor((width - (count - 1) * GAP) / count)
    for index, one in ipairs(buttons) do
      one:resize(each, ROW_H)
      one:place(MARGIN + (index - 1) * (each + GAP), bottom)
    end
  end
end

local function settle()
  local c = ui.caps()
  if c.width == wid and c.height == hei then
    layout()
    paint_page()
    return
  end
  wid, hei = c.width, c.height
  layout()
  paint_page()
end

local function build()
  wid, hei = ui.caps().width, ui.caps().height
  named = {}

  local why = nil
  big, why = ui.label(2, 72)
  if big == nil then
    console.print('battery: no big number (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  big.text = '--'
  big.scale = 7
  big.align = 'left'

  detail, why = ui.label(2, 3)
  if detail == nil then
    console.print('battery: no line under the number (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  detail.text = ''
  detail.scale = 2

  chart, why = ui.chart(2, 2)
  if chart == nil then
    console.print('battery: no chart (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end

  legend, why = ui.label(2, 3)
  if legend == nil then
    console.print('battery: no line of the window (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  legend.text = ''
  legend.scale = 2

  -- **El peu són dues línies i no una**: a label does not wrap (docs/30) and the sentence of this foot is longer than the
  -- page at the size of a text, so the second line is its own label instead of a sentence cut with "..." —the rule the
  -- paper project wrote for its notice, which had two lines of room (docs/92).
  note, why = ui.label(2, 3)
  if note == nil then
    console.print('battery: no line of the foot (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  note.text = ''
  note.scale = 2

  note2, why = ui.label(2, 3)
  if note2 == nil then
    console.print('battery: no second line of the foot (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  note2.text = ''
  note2.scale = 2

  -- **Tres botons i no un selector**: the window of a chart is a thing a person presses while looking at the picture, and a
  -- card over the page would hide the picture it is choosing from (docs/30, docs/92).
  buttons = {}
  for index, hours in ipairs(Ranges) do
    local one = nil
    one, why = ui.button(2, 2)
    if one == nil then
      console.print('battery: no button for ' .. hours .. ' h (' .. tostring(why and why.code or 'the memory') .. ')')
      return false
    end
    one.title = hours .. ' h'
    one.primary = (index == range) and true or false
    local name = 'range' .. hours
    one.on_focus = function(self, on)
      if on then shape.focus = name end
    end
    one.on_click = function()
      range = index
      write_range()
      save_state()
      layout()
      paint_page('gc16')
    end
    named[name] = one
    buttons[#buttons + 1] = one
  end

  if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end
  return true
end

-- **El que ha sonat es fa dins del ganxo i no al bucle** (0.189.145): una alarma que venç amb l'app tancada obre una
-- **finestra**, i una finestra crida `on_alarm` i tot seguit tanca l'app — o sigui que una mostra deixada per al bucle de la
-- pàgina és una mostra que no es pren mai, que és exactament el que l'alarma havia de fer. El ganxo no pot esperar, i
-- apuntar una mostra no espera res: llegeix el que la placa diu, l'afegeix a la llista i escriu el fitxer (docs/30, docs/60).
local due = nil
local loaded = false

function on_alarm(id, payload, meta)
  local mine = (payload == 'sample') or (meta ~= nil and meta.missed ~= nil and meta.missed > 0)
  if not mine then return end
  -- **En una finestra el bloc de dalt no ha llegit res**: the entry of a window runs only down to this point, so the log has to
  -- be read here or the sample would be saved over a day of samples with one line (docs/30, docs/60).
  if not loaded then
    read_log()
    loaded = true
  end
  record()
  due = true
end

-- **Una finestra només porta la crida de l'alarma** (0.189.145): the system loads the entry to open the app and calls
-- `on_alarm` right away, and without this door every window would build a page nobody sees and arm an alarm that the system
-- would answer with another window, for ever (docs/30, provat a la placa).
if not console.is_foreground() then return end

read_range()
read_log()
loaded = true
if not build() then return end
local was = load_state()
if was ~= nil and was.focus ~= nil then shape.focus = was.focus end
-- **Un nom que no és cap dels botons no és un focus, és una pàgina sense cercle**: the name is valued before it is used
-- (docs/30, docs/45 §5 bis).
if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end

-- **Una mostra en obrir-se**, que és el que fa que la pàgina tingui un punt abans que passin deu minuts, i **una alarma
-- per a les altres**: the app records, and then the system calls it every ten minutes whether it is in front or not.
record()
console.print(string.format('battery: %d h window, %d in the log', Ranges[range], #samples))
layout()
paint_page()

-- **Les promeses velles d'aquesta app es donen per acabades abans d'armar-ne una de nova**: the queue of the system survives
-- a restart and a copy of the board, and an app that comes back with six of its own cannot arm another one (docs/30).
-- **I només les seves**: fins a la 0.189.172 aquest bucle anul·lava la cua sencera —la del `meteo` i la de l'`agenda` també—
-- i obrir la `battery` esborrava els avisos de les altres apps, que els tornaven a armar a la càrrega següent (docs/30, docs/60).
local function forget_old_promises()
  local ok, mine = pcall(alarm.list)
  if not ok or type(mine) ~= 'table' then return end
  for _, one in ipairs(mine) do
    if type(one) == 'table' and one.id ~= nil and one.app == app.id then pcall(alarm.cancel, one.id) end
  end
end

forget_old_promises()
-- **`repeat` és una paraula reservada de Lua** i una taula escrita `{repeat = 600}` no s'analitza: el camp s'escriu amb els
-- claudàtors i és el que la cua del sistema espera (docs/30, docs/60).
local armed_ok, id = pcall(alarm.at, { in_ms = kSampleMs, payload = 'sample',
                                       ["repeat"] = math.floor(kSampleMs / 1000), persist = true })
if armed_ok then
  armed = id
else
  trouble = 'No alarm: this page only records when it is opened.'
end
save_state()

-- **El cercle i la finestra es desen quan la pàgina marxa**, que és el pacte de la sessió `state` (docs/30, docs/45).
function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

while true do
  if due ~= nil then
    due = nil
    paint_page()
  end

  -- **Un canvi de mida torna a fer la pàgina sencera**: the system makes a new view for the whole glass or for the band of
  -- the keyboard, and every canvas of the page comes back dead, so the page is built again in the box it has now.
  local c = ui.caps()
  if c.width ~= wid or c.height ~= hei then
    wid, hei = c.width, c.height
    if build() then
      local again = load_state()
      if again ~= nil and again.focus ~= nil then shape.focus = again.focus end
      if shape.focus ~= nil and named[shape.focus] ~= nil then ui.focus(named[shape.focus]) end
      layout()
      paint_page()
    end
  end

  -- **Un toc al paper pren una mostra d'ara**: a gesture no control has taken is the page's, and the foot of the page says
  -- so (docs/30, docs/90).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'tap' then
      if record() then paint_page() end
    end
    gesture = touch.gesture()
  end

  sys.delay(150):await()
end
