-- energy: **el consum de la placa, i fins on es pot saber** (0.190.156, l'amo: *«Fes una app lua "Energy" que doni tota la informació
-- sobre el consum de la placa i tots els dispositius que es pugui mesurar el consum. Consulta també la batería per obtenir mes dades
-- eléctrics»*).
--
-- **El número gran és la potència**, que és el que una persona vol saber i no el corrent: mil·liwats surten del voltatge de la cel·la
-- pel corrent que el comptador de coulombs hi veu, i el signe diu de quina banda va. Sota hi ha el que la placa pot dir de la seva
-- pròpia bateria (salut, cicles, temperatura) i de la seva entrada (quin cable és, què hi entra, i el rail del sistema).
--
-- **I la female, dita en una línia**: aquesta placa **no té cap resistència en sèrie per dispositiu**, o sigui que el que es mesura
-- és el conjunt —el vidre, la ràdio, la targeta i el xip alhora— i qualsevol número per part seria una repartició inventada. La
-- línia dels dispositius diu què hi ha soldat, que és el que es podria arribar a mesurar el dia que hi hagués un shunt; el que no
-- fa és inventar-se un consum per a cada un (docs/50, docs/60, docs/95).
--
-- **Les portes que fa servir**: `power.battery` (la cel·la i el que el comptador en sap), `power.charger` (el BQ25896), `power.cpu`,
-- `sys.caps`/`sys.board`, `ui.*` i `sys.session` per a la forma de la vista. Cap d'elles no és nova.

-- **La cara de les marques d'aquesta app**, carregada del seu propi `res` com la del TimeClock (docs/45). Sense ella el botó
-- queda amb la marca del sistema i el toc fa el mateix.
local marks = ui.load_font('/res/keys.ttf')
local kRefresh = string.char(1)

local caps = sys.caps()
if not caps.display then
  console.print('energy: this board has no glass')
  return
end

-- **`v2`** (0.190.156): el títol de la pàgina s'ha tret —la barra de dalt ja diu «Energy»— i la forma del bloc canvia.
local kLayout = 'v3'
local kStateBytes = 1024
local kPageMode = 'gld16'     -- text sobre paper: setze grisos i sense el fantasma de la lectura d'abans

local MARGIN = 20
local GAP = 12
local TITLE_H = 30
-- **L'alçada del número va amb el cos** (0.190.160): a label cuts what does not fit in its box, o sigui que amb seixanta-quatre
-- píxels un cos de nou es quedava sense cap fila visible —el forat hi era i el número no— (docs/30, docs/92).
local BIG_H = 130
local DETAIL_H = 40
local ROW_H = 52
-- **Les mides de la pàgina apaïsada** (0.190.156): the T5 S3 Pro has 960 px of width and 540 of height, i el que hi sobra és ample,
-- o sigui que el cos del text hi pot ser més gran que en un vidre de 540 d'ample (docs/30, docs/92).
local BIG_H_WIDE = 150
local DETAIL_H_WIDE = 40
local ROW_TEXT_WIDE = 40
local NOTE_H_WIDE = 24

-- **El bloc de `state`, copiat de la resta d'apps** (docs/30): un format que només llegeix un fitxer és un format que no llegeix ningú.
local state_block = nil

local function the_state()
  -- **Un bloc que el sistema s'ha quedat no es pot fer servir** (0.192.55): the session blocks are the first thing the allocator
  -- gives back when the memory is short, and every call on one that is gone raises `the session memory was given back`, which ends
  -- the app. It is asked again —that is what the contract says an app must do— and if there is no room the page is built as if it
  -- were the first time (docs/30, docs/75).
  if state_block ~= nil and not state_block:alive() then state_block = nil end
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'focus', 'page', 'tab' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
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

-- **La forma d'aquesta pàgina**: cap control amb cercle —el toc al paper és la lectura—, o sigui que el bloc només recorda la
-- versió de la forma i el que hi hagi en un futur (docs/30).
local shape = { focus = 'again' }

-- **Una etiqueta per fila i no una de sola amb salts** (0.190.156): a label is one line of text —the shell has its own control for
-- a page of them— i el que va sortir de la primera prova va ser «Battery 76% 874/1150 mAhCell 100% hea...»: dues files enganxades i
-- tallades. Les files són un banc de controls i el que no hi cap es queda buit (docs/30, docs/92).
local kRows = 14
local big, detail, again, note, note2
local rows = {}
local kShown = 0          -- how many of them the glass can hold at the body they are drawn with
local wid, hei = 0, 0
-- **El botó és un control i la icona hi va a dins** (0.190.160, l'amo: *«he demanat explícitament un botó (del UI) amb una icona
-- dins de recarregar»*): a picture painted over a rectangle was a gesture and not a control, o sigui que el toc el llegeix el
-- botó i la marca la pinta ell (docs/30, docs/92).
--
-- **La fletxa ve de FontAwesome** (l'amo: *«usa una lletra de fontawesome home!»*), com les cinc tecles del TimeClock i la marca
-- de Life: `tools/make_keys.py --marks refresh=0x01 --drop 350 --out apps/energy/res/keys.ttf` en treu els contorns de `refresh`
-- (U+F021) i els deixa en una cara de dos glifs on el codi `\1` és la fletxa. **El `drop` és el centrat** (0.190.160, l'amo:
-- *«centra una mica l'icona dins el botó»*): la línia que un botó centra és `face.height` (onze píxels per pas) i els contorns es
-- dibuixen a nou, o sigui que tota marca d'aquestes surt dos píxels per pas per sobre del mig i el dibuix baixa tres-centes
-- cinquanta unitats per tornar-hi (docs/30, docs/45, docs/92).
-- **La mida del botó és la del TimeClock** (0.190.160, l'amo: *«Mira el tamany del botó a TimeClock»*): the eye of that app is a
-- mark inside a control of seventy-eight by sixty-three, o sigui que aquest refresc fa el mateix —una marca a la cara d'un botó
-- de la mida de la casa i no una imatge que s'estiri fins a la vora— (docs/30, docs/45).
local ICON_W, ICON_H = 78, 63
local button_x, button_y = 0, 0
local asked = false      -- the button asked for a reading; the loop is what paints it

-- ---------------------------------------------------------------- els números

-- **Un camp que el xip no diu no s'inventa**: `nil` es pinta com un guió i mai com un zero, perquè un zero és una mesura i una
-- absència no ho és (docs/30).
local function number_or_dash(value, format)
  if value == nil then return '--' end
  return string.format(format, value)
end

-- **La potència en mil·liwats**, que és el voltatge de la cel·la pel corrent. `mv * ma / 1000` i no `mv * ma / 1000000` perquè el
-- que surt és mW i no W: una placa que consumeix 0,4 W a la vora d'un número que sembla zero no diu res (docs/95).
local function milliwatts(mv, ma)
  if mv == nil or ma == nil or mv <= 0 then return nil end
  return (mv * ma) / 1000
end

-- **La frase del corrent**: qui alimenta qui. És el signe del comptador i no una deducció del cable, perquè la placa pot estar
-- endollada i no estar carregant (docs/50, docs/60).
-- **Una paraula i no una frase** (0.190.156): the detail line shares the column with the big number on the wide glass, i «feeding
-- the board» a cos de dos ja no hi cap; el que es perd no és el sentit, perquè el signe i la paraula diuen el mateix (docs/92).
local function flow_words(ma)
  if ma == nil then return 'no current' end
  if ma < 0 then return 'feeding' end
  if ma > 0 then return 'charging' end
  return 'idle'
end

local function clock_words()
  local ok, one = pcall(time.caps)
  if not ok or type(one) ~= 'table' then return '' end
  if type(one.chip) ~= 'string' or one.chip == '' then return '' end
  return one.chip
end

-- **Els sensors, com es diuen en una fila** (0.190.157): the app names a magnitude and a place and not a chip, o sigui que el
-- mateix codi pinta un SHT30, un BQ27220 o un INA219 sense saber que existeixen. El nom surt de la taula i, si un dia la placa
-- en té un de nou, el que falta és una línia aquí i no una branca al mig de la pàgina (docs/30, docs/50).
-- **Les magnituds d'aquesta pàgina són les que es paguen** (0.190.161, l'amo: *«Perqué surten dades comn temperatura i humitat?
-- l'app Energy es per saber quan consumeix cada dispositiu que es pugui medir ... tota la informació eléctica, consum del
-- sistema»*): volts, ampers, els ampers-hora de la cel·la i —si algun dia una placa en té— els watts. La temperatura i la
-- humitat són d'una altra pàgina: una placa que esbufega no és una placa que gasta, i el que aquí es llegeix és el compte.
local kElectrical = { voltage = true, current = true, charge = true, power = true }

local kShortName = {
  ['charge|cell'] = 'Charge',
  ['voltage|cell'] = 'Cell V',
  ['voltage|input'] = 'Input V',
  ['voltage|system'] = 'System V',
  ['current|cell'] = 'Cell I',
  ['current|input'] = 'Input I',
  ['power|cell'] = 'Cell P',
  ['power|input'] = 'Input P',
  ['power|system'] = 'System P',
}

local function sensor_name(one, kind)
  local key = tostring(kind or one.kind) .. '|' .. tostring(one.where)
  return kShortName[key] or key
end

-- **La unitat mana els decimals**: volts a dos, graus a un, mil·liamperes i tants per cent sense decimals. Una fila que diu
-- «350.00 mA» és una precisió que el xip no té (docs/30, docs/95).
local function sensor_words(one, value)
  if one.unit == 'V' then return string.format('%.2f V', value) end
  if one.unit == 'C' then return string.format('%.1f C', value) end
  if one.unit == '%' then return string.format('%.0f %%', value) end
  return string.format('%.0f %s', value, one.unit)
end

-- **Les files del cos**: first what the battery is as a whole —the charge, its health, what the cable is— and then **every
-- magnitude the board can measure**, in the order of its ids, que és el que l'app pot saber sense preguntar de quin xip ve
-- (docs/30, docs/50). El que no respon no deixa una fila buida.
local function body_lines()
  local lines = {}
  local function row(name, value)
    if value == nil or value == '' then return end
    lines[#lines + 1] = string.format('%-9s %s', name, value)
  end

  local okb, b = pcall(power.battery)
  if okb and type(b) == 'table' then
    -- **La càrrega és una magnitud i ve dels sensors** (`power.charge[0]`), o sigui que aquí només hi va el que no ho és: la
    -- capacitat de la cel·la (0.190.158). Una pàgina amb dos «76%» és una pàgina que no sap què és cada número.
    if b.mah ~= nil and b.mah_full ~= nil then
      row('Battery', string.format('%d/%d mAh', b.mah, b.mah_full))
    elseif b.pct ~= nil then
      row('Battery', string.format('%d%%', b.pct))
    end
    -- **Curta, que a la T5 S3 Pro la fila va a cos de tres i només hi cap un quart de món** (0.190.156): at that size a column of
    -- 552 px holds about twenty-six characters, o sigui que «100% health, 0 cycles» no hi entra i «100%, 0 cycles» sí (docs/92).
    if b.health ~= nil or b.cycles ~= nil then
      local parts = {}
      if b.health ~= nil then parts[#parts + 1] = string.format('%d%%', b.health) end
      if b.cycles ~= nil then parts[#parts + 1] = string.format('%d cycles', b.cycles) end
      row('Cell', table.concat(parts, ', '))
    end
  end

  local okc, c = pcall(power.charger)
  if okc and type(c) == 'table' then
    local state = c.charge
    if c.input ~= nil and c.input ~= 'no input' then
      state = string.format('%s, %s', c.input, c.charge or '?')
    end
    row('Charger', state)
  end

  -- **I totes les magnituds que la placa té, preguntades pel seu id** (0.190.157): the list is the hardware's answer and not a
  -- guess of this file, o sigui que una placa amb tres termòmetres en pinta tres i una que no en té cap no en pinta cap.
  local oks, list = pcall(sensors.list)
  if oks and type(list) == 'table' then
    -- **El que cada punt de mesura gasta** (0.190.161): a place that says its volts and its amperes at once says its watts too,
    -- o sigui que el compte es fa aquí i no s'inventa —el corrent de la cel·la és el de la placa sencera, i el de l'entrada és el
    -- que arriba pel cable— (docs/30, docs/95).
    local at = {}
    for _, one in ipairs(list) do
      if kElectrical[one.kind] then
        -- **Cada sensor es llegeix ell** (0.190.158): `one.read()` i no una crida del mòdul amb un id, que és el que l'amo va
        -- demanar —l'objecte és el sensor i el número el dona ell— (docs/30).
        local fine, value = pcall(one.read)
        if fine and type(value) == 'number' then
          row(sensor_name(one), sensor_words(one, value))
          local place = tostring(one.where)
          at[place] = at[place] or { where = place }
          at[place][one.kind] = value
          at[place].unit = one.unit
        end
      end
    end
    -- **I la potència de cada un d'ells, darrere de les seves dues xifres**: the sign of the current is kept, o sigui que un
    -- número negatiu és potència que surt i un de positiu la que entra (docs/30, docs/95).
    for _, place in ipairs({ 'cell', 'input', 'system' }) do
      local one = at[place]
      if one ~= nil and one.voltage ~= nil and one.current ~= nil then
        local mw = milliwatts(one.voltage, one.current)
        if mw ~= nil then
          row(sensor_name({ kind = 'power', where = place }, 'power'), string.format('%d mW', math.floor(mw + 0.5)))
        end
      end
    end
  end

  -- **Els noms són els de `sys.caps()` i no uns d'inventats** (`hal::cap_name`): display, touch, net_wifi, sd, battery, imu, env,
  -- rtc i littlefs, que és el que aquesta placa té soldat i el que una pàgina pot anomenar sense mentir (docs/30, docs/50).
  local has = {}
  for _, one in ipairs({ 'display', 'touch', 'net_wifi', 'sd', 'battery', 'imu', 'env', 'rtc', 'littlefs' }) do
    if caps[one] then has[#has + 1] = one end
  end
  -- **La llista es parteix tota sola segons l'amplada que hi cap** (0.190.156): at a wide glass about twenty-four characters fit
  -- in a row of this size, o sigui que els nou noms baixen a dues o tres files en lloc de sortir amb punts suspensius (docs/92).
  if #has > 0 then
    local line, first = {}, true
    local function flush()
      row(first and 'Parts' or '', table.concat(line, ', '))
      first = false
      line = {}
    end
    for _, one in ipairs(has) do
      local candidate = (#line == 0) and one or (table.concat(line, ', ') .. ', ' .. one)
      if #candidate > 24 and #line > 0 then flush() end
      line[#line + 1] = one
    end
    if #line > 0 then flush() end
  end

  -- **Els megahercis van al final** (0.190.160): the parts are what a person can measure and the clock of the cpu is what
  -- changes by itself, o sigui que en un vidre que només té dotze files el que es queda fora és aquesta (docs/30, docs/92).
  local mhz = power.cpu()
  if mhz ~= nil then row('Board', string.format('%d MHz', mhz)) end
  return lines
end

-- ---------------------------------------------------------------- la pàgina

local function refresh()
  local mv, pct, ma = nil, nil, nil
  local okb, b = pcall(power.battery)
  if okb and type(b) == 'table' then
    mv = b.mv
    pct = b.pct
    ma = b.ma
  end

  local mw = milliwatts(mv, ma)
  if big ~= nil then
    if mw ~= nil then
      local shown = math.abs(mw)
      if shown >= 1000 then
        big.text = string.format('%.1f W', shown / 1000)
      else
        big.text = string.format('%d mW', math.floor(shown + 0.5))
      end
    elseif pct ~= nil then
      big.text = string.format('%d%%', pct)
    else
      big.text = '--'
    end
  end
  if detail ~= nil then
    if mv ~= nil then
      detail.text = string.format('%.3f V   %s mA', mv / 1000, number_or_dash(ma, '%+d'))
    else
      detail.text = 'this board cannot read its cell'
    end
  end
  -- **Les files, una a una i en ordre de prioritat**: the first ones are the battery and the charger and the last ones are the
  -- details, o sigui que un vidre baix talla pel final i no pel mig (docs/92).
  local lines = body_lines()
  for i = 1, kShown do
    if rows[i] ~= nil then rows[i].text = lines[i] or '' end
  end
  -- **Dues línies curtes i a cos d'u** (0.190.156): a wide glass the left column is 344 px, o sigui que quaranta-nou caràcters és
  -- tot el que hi cap; la frase sencera hi arriba partida (docs/92).
  if note ~= nil then
    note.text = 'One number for the board:'
  end
  if note2 ~= nil then
    note2.text = 'no shunt per part.'
  end
end

-- **Dues disposicions i no una d'estirada** (0.190.156, l'amo: *«Fes el text dels valors mes gran, es una pantalla molt petita i
-- te molta resolució»*). The T5 E-Paper S3 Pro is **960 by 540** —wide and short— and the Paper is 540 by 960: the same page in one
-- column would leave the big number squeezed and the rows tiny on the board a person actually holds, o sigui que quan el vidre és
-- més ample que alt la pàgina es parteix en dues: la potència a l'esquerra i les files a la dreta, cadascuna amb tota l'alçada
-- (docs/30, docs/92).
-- **El cos del text el decideix el vidre, i es torna a decidir a cada col·locació** (0.190.156): the first version only set it when
-- the page was built, o sigui que un vidre que canviava de forma es quedava amb el cos de l'anterior —el que es va veure a la prova
-- apaïsada, amb la potència petita i les files de dos— (docs/30, docs/92).
local function set_the_body(wide)
  -- **Set és el que hi cap i no dotze** (0.190.156): at scale 12 the six characters of «475 mW» need 500 px in a column of 344,
  -- o sigui que el label els retallava amb quadres; i per sobre del miler el que es llegeix és «8.4 W» i no «8400 mW» (docs/92).
  -- **Sis i no dotze** (0.190.156): at scale 12 the six characters of «475 mW» need 500 px in a column of 344, o sigui que el label
  -- els retallava amb quadres; i per sobre del miler el que es llegeix és «8.4 W» i no «8400 mW». Sis dona marge, perquè el retall
  -- del label no és exactament l'amplada que se li dona i un número tallat no és un número (docs/92).
  -- **Sis és el sostre de debò, i no per l'amplada** (0.190.160, l'amo: *«Fes els texts mes grans!»*): a cos vuit o nou la pàgina
  -- perdia el «mW» i es quedava amb el «0» —el lector d'outlines pinta cada glif en un bloc de seixanta-quatre píxels
  -- (`kOutlineMaxWidth`), o sigui que a noranta píxels de cos les lletres amples (`m`, `W`) no hi caben i el número es queda amb
  -- forats— (docs/30, docs/92). El que sí que puja és tot el que és de text: el detall i les files a tres i les notes a dos.
  if big ~= nil then big.scale = 6 end
  -- **Tres i no dos, amb la línia escurçada**: «3.960 V  -120 mA  feeding» a cos de tres no cap a la columna de 344 px, i el que
  -- sobra és la paraula —el signe del corrent ja diu de quina banda va— (docs/92).
  if detail ~= nil then detail.scale = 3 end
  for i = 1, kRows do
    if rows[i] ~= nil then rows[i].scale = 3 end
  end
  if note ~= nil then note.scale = 2 end
  if note2 ~= nil then note2.scale = 2 end
end

local function layout()
  local c = ui.caps()
  wid, hei = c.width, c.height
  local wide = wid > hei
  set_the_body(wide)

  if wide then
    local left_w = math.floor(wid * 0.40) - 2 * MARGIN
    local x = MARGIN
    local y = MARGIN
    button_x, button_y = wid - MARGIN - ICON_W, MARGIN
    if again ~= nil then
      again:resize(ICON_W, ICON_H)
      again:place(button_x, button_y)
    end
    if big ~= nil then
      big:resize(left_w, BIG_H_WIDE)
      big:place(x, y)
      y = y + BIG_H_WIDE + GAP
    end
    if detail ~= nil then
      detail:resize(left_w, DETAIL_H_WIDE)
      detail:place(x, y)
    end
    -- El peu de l'esquerra: el botó, i les dues notes del damunt.
    local note_h = NOTE_H_WIDE
    if note2 ~= nil then
      note2:resize(left_w, note_h)
      note2:place(x, hei - MARGIN - note_h)
    end
    if note ~= nil then
      note:resize(left_w, note_h)
      note:place(x, hei - MARGIN - 2 * note_h - 4)
    end
    -- I les files, amb tota l'alçada del vidre per a elles.
    local rows_x = MARGIN + left_w + 2 * GAP
    local rows_w = wid - rows_x - MARGIN
    -- **Les files comencen sota el botó** (0.190.159): the button lives in the top right corner of the glass, i la primera fila
    -- de la columna de la dreta hi passaria per sobre; seixanta-vuit píxels de marge a dalt són el que necessita (docs/92).
    local rows_top = MARGIN + ICON_H + 12
    local available = hei - rows_top - MARGIN
    -- **El text no s'encongeix per fer-hi cabre una fila** (0.190.160, l'amo: *«Fes els texts mes grans!»*): the height of a row
    -- is the height of the line at body three and no less, o sigui que la pàgina ensenya les files que hi caben i les que no es
    -- queden fora —i la llista ja va per prioritat, amb els aparells al davant dels megahercis— (docs/30, docs/92).
    kShown = 0
    for i = 1, kRows do
      if rows[i] ~= nil and (i - 1) * ROW_TEXT_WIDE + ROW_TEXT_WIDE <= available then
        rows[i]:resize(rows_w, ROW_TEXT_WIDE)
        rows[i]:place(rows_x, rows_top + (i - 1) * ROW_TEXT_WIDE)
        kShown = i
      end
    end
    return
  end

  button_x, button_y = wid - MARGIN - ICON_W, MARGIN
  if again ~= nil then
    again:resize(ICON_W, ICON_H)
    again:place(button_x, button_y)
  end
  if big ~= nil then
    big:resize(wid - 2 * MARGIN - ICON_W - 4, BIG_H)
    big:place(MARGIN, MARGIN)
  end
  if detail ~= nil then
    detail:resize(wid - 2 * MARGIN, DETAIL_H)
    detail:place(MARGIN, MARGIN + BIG_H + GAP)
  end
  local body_top = MARGIN + BIG_H + DETAIL_H + 2 * GAP
  local note_h = 24
  -- **El peu deixa lliures les vuitanta files de baix** (0.190.157): they are the system's button under the glass —a centred band
  -- of 120 by 80 px—, o sigui que una app que hi posa un botó veu com el sistema se'l menja i tanca la pàgina; el que hi ha
  -- darrere d'aquest comentari és exactament el que va passar amb «Read again» a la placa (docs/30, docs/50).
  local foot = hei - MARGIN - 80
  if note2 ~= nil then
    note2:resize(wid - 2 * MARGIN, note_h)
    note2:place(MARGIN, foot - note_h)
  end
  if note ~= nil then
    note:resize(wid - 2 * MARGIN, note_h)
    note:place(MARGIN, foot - 2 * note_h - 4)
  end
  -- **Les files es reparteixen l'espai que queda i prou**: no hi ha desplaçament en aquesta pàgina, o sigui que el que no hi cap es
  -- queda fora i no cal amagar-ho (docs/30).
  local available = foot - 2 * note_h - 4 - GAP - body_top
  kShown = 0
  for i = 1, kRows do
    if rows[i] ~= nil and (i - 1) * ROW_TEXT_WIDE + ROW_TEXT_WIDE <= available then
      rows[i]:resize(wid - 2 * MARGIN, ROW_TEXT_WIDE)
      rows[i]:place(MARGIN, body_top + (i - 1) * ROW_TEXT_WIDE)
      kShown = i
    end
  end
end

local function build()
  local saved = load_state()
  if saved ~= nil and type(saved.focus) == 'string' then shape.focus = saved.focus end

  -- **El cos del text el decideix el vidre** (0.190.156): 960 px d'ample aguanten un cos de tres i una potència de nou; 540 no.
  local why = nil
  big, why = ui.label(2, BIG_H)
  if big == nil then
    console.print('energy: no power line (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  big.text = '--'

  detail, why = ui.label(2, DETAIL_H)
  if detail == nil then
    console.print('energy: no detail line (' .. tostring(why and why.code or 'the memory') .. ')')
    return false
  end
  detail.text = ''

  -- **Una fila que no s'ha pogut crear no tomba la pàgina** (0.190.156): a board with little heap left loses a row and keeps the
  -- number that matters, perquè una home sobtada no diu res i una pàgina amb una fila menys sí (docs/30, docs/90).
  for i = 1, kRows do
    local one = nil
    one, why = ui.label(2, 30)
    if one == nil then
      console.print('energy: no row ' .. i .. ' (' .. tostring(why and why.code or 'the memory') .. ')')
    else
      one.text = ''
      rows[i] = one
    end
  end

  again, why = ui.button(ICON_W, ICON_H)
  if again ~= nil then
    -- **La marca és el títol del botó, amb una cara de l'app** (0.190.160): it is the road of the TimeClock and of Life —a face
    -- of the app's own folder with the figure at the code one, because a text here is one byte a character and `↻` is three—, i
    -- així el botó pinta la fletxa amb la seva tinta i el seu cos i no cal cap imatge a sobre (docs/30, docs/45).
    if marks ~= nil then
      again:font(marks)
      again.title = kRefresh
      -- **Quatre i no cinc** (0.190.160, l'amo: *«centra una mica l'icona dins el botó»*): the figure of FontAwesome fills its
      -- em from edge to edge, o sigui que a cos cinc la fletxa tocava el marc per dalt i el deixava amb cinc píxels de buit a
      -- baix; a cos quatre la tinta és un terç de la cara i queda al mig (docs/30, docs/92).
      again.scale = 4
    else
      -- A board whose disk lost the face keeps a key that says what it does, which is what the TimeClock does too.
      again.icon = 'reboot'
    end
    again.primary = true
    -- **El callback només demana i el bucle ho fa** (0.190.156): pintar dins del toc era el que tancava l'app.
    again.on_click = function() asked = true end
  else
    console.print('energy: no button (' .. tostring(why and why.code or 'the memory') .. ')')
  end

  note, why = ui.label(2, 22)
  if note ~= nil then
    note.text = ''
  end

  note2, why = ui.label(2, 22)
  if note2 ~= nil then
    note2.text = ''
  end

  return true
end

local function paint()
  ui.clear(15)
  layout()
  refresh()
  save_state(shape)
  ui.present(kPageMode)
end

if not build() then return end
paint()

console.print('energy: the board and its cell, in one page')

-- **I el bucle que la manté viva** (0.190.156, l'amo: *«Cada cop que obro l'app torna a home»*). A page is not a script that runs
-- and ends: **when the last line returns, the kernel closes the app and opens the home**, o sigui that a page painted perfectly and
-- followed by nothing is a page that flashes and goes away. What the loop does is keep the turn of the kernel going and ask for the
-- page again —a present with nothing new draws nothing, i el toc al paper és el que demana una lectura nova— i
-- arranges the page once more only when the glass has changed size under it (docs/30, docs/90).
-- **Cada torn, protegit** (0.190.156, l'amo: *«Es una petada? perqué no es visible el modal d'error lua amb totes les dades
-- possibles de la petada?????»*). The kernel paints its page of the error only when the interpreter says the app failed, i una
-- pàgina que s'apaga sense dir res deixa exactament el dubte que va deixar aquesta: el torn pot caure per una crida del maquinari
-- —the gauge on the I2C that the finger has just been talking to, a block of the session that the memory took— i el que ha de
-- passar aleshores és que la pàgina ho digui i segueixi, no que l'app desaparegui. Cada torn va dins un `pcall` i el que en surt
-- va a la consola amb el seu missatge (docs/30, docs/90).
-- **I si algú la tanca, que ho digui** (0.190.156): an app that ends without a word is an app nobody can tell from one that
-- crashed, o sigui que les tres comiades del sistema deixen una línia cadascuna a la consola (docs/30, docs/40).
function on_pause() console.print('energy: paused') end
function on_save() console.print('energy: saving') end
function on_close() console.print('energy: closed by the system') end

while true do
  local fine, why = pcall(function()
    local now = ui.caps()
    if now.width ~= wid or now.height ~= hei then
      layout()
      ui.present(kPageMode)
    elseif asked then
      asked = false
      refresh()
      save_state(shape)
      ui.present(kPageMode)
    else
      ui.present()
    end
  end)
  if not fine then console.print('energy: the turn failed: ' .. tostring(why)) end
  sys.delay(50):await()
end

