-- weather: **les dues respostes d'OpenWeatherMap al disc, i el que hi diu, amb l'edat ben visible** (0.199.0). Fa això:
--
--   1. Mira si la placa té credencials a `/data/weather/openweather.txt` (`appid=`, `lat=`, `lon=`). Si no les té, s'ha
--      acabat: no es baixa res i la pàgina diu que es deixa escriure amb l'engranatge.
--   2. Si les té, mira `now.json` i `forecast.json` de `/data/weather/`: **l'hora del fitxer** diu si són més vells de
--      vint-i-quatre hores. Si ho són —o si no hi són— es baixen de nou les dues respostes de la 2.5.
--   3. I **ensenya el que hi ha als fitxers**, sigui d'ara o de fa hores. El títol de la pàgina és **el lloc** —el `name` de
--      la resposta d'ara— i sota, més petit, l'avís de quan és: `Data from 3 h ago` o `Data from just now`. Una dada vella
--      dita com a vella és una dada que serveix; la mateixa dada sense dir-ho és una mentida. **Els dies són un `ui.list`** i la
--      pàgina es pinta amb dues tintes —tinta i paper— (0.199.6).
--
-- **L'edat és la del fitxer i no una marca a part** (0.199.0): littlefs desa l'hora de modificació i la targeta també, i
-- `fs.stat(...).mtime` la dona en segons de l'època. Un `mtime` de zero vol dir que aquell disc no la sap —una memòria no
-- té rellotge—, i llavors es baixa, perquè el que no es pot provar que és d'avui no es dona per bo (docs/30, docs/70).
--
-- **I la clau i el lloc s'escriuen a la pàgina** (0.199.4, l'amo: *«Quan l'app falla no deixa accedir a Settings de l'app»*):
-- el botó d'engranatge obre la pàgina de `appid`, `lat` i `lon` i desa **el mateix fitxer que llegeix l'app**, o sigui que una
-- placa sense credencials —o amb una clau que el servei refusa— es pot arreglar des del vidre i no demanant un editor de text.
-- Una app que diu «escriu-ho en un fitxer» i no deixa escriure'l no és una app acabada.
--
-- **I per això sí que té sessió `state`** (0.199.4): dues pàgines i tres caixes de text són una forma que ha de tornar com la
-- persona la va deixar —el text escrit i encara no desat també, perquè la banda del teclat canvia l'alçada de la vista i refà
-- la pàgina— (docs/30 § «La sessió `state`»). Es tanca amb el que tanca qualsevol pàgina d'aquesta casa: la creu de la targeta
-- de la barra i el pessic de gran a petit (docs/30, docs/45).

local function say(t) console.print('weather: ' .. t) end

local CONFIG = fs.data('openweather.txt')
local NOW_JSON = fs.data('now.json')
local FORECAST_JSON = fs.data('forecast.json')

local kDay = 24 * 60 * 60
local kTimeoutMs = 22000

local DEG = string.char(176)
-- **Dues tintes i prou** (0.199.6, l'amo: *«NO USIS COLOR GRIS CLAR»*): la jerarquia d'aquesta pàgina la fa **la mida**, com a
-- `meteo` i a `agenda`, i un gris clar en aquest vidre no és un to més fluix sinó una lletra que es llegeix pitjor (docs/30).
local INK, PAPER = 0, 15

-- The sizes of the paper project in pixels of line, asked of the face the system draws with: the same page at the same
-- physical size on both boards (docs/45 § 3).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end
  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end
  return scale
end

local kSmall = scale_for(33)
local kBody = scale_for(43)
local kTitle = scale_for(57)
local kBig = scale_for(96)

-- The margin, the gear and the strip are the house's numbers —the same as the Jellyfin—: a finger gets 48 px of gear and
-- 63 px of tab, and the page of the app starts under them (docs/30, docs/45).
local Margin = 12
local GearW, GearH = 56, 48
local TabH = 63
local Gap = 8

local has_glass = sys.caps().display and true or false
local wid, hei = 0, 0
if has_glass then
  local c = ui.caps()
  wid, hei = c.width, c.height
end


-- ---------------------------------------------------------------- la forma que es recorda

-- **El text escrit i encara no desat és forma i no dada** (0.199.4): la banda del teclat canvia l'alçada de la vista i la
-- pàgina es torna a fer, i el que hi ha a les caixes ha de venir de l'app i no del control que se n'ha anat (docs/30).
local kLayout = 'v2'
local kStateBytes = 1024
local state_block = nil

local page = 'main'    -- 'main' (Now), 'days', 'hours' o 'settings'
local focus_ = ''      -- el nom del control on era el cercle
local typed = nil      -- el que s'està escrivint i no s'ha desat
local asked = nil      -- el que ha demanat un ganxo i fa el bucle: 'gear', 'save' o 'tab'
local asked_tab = 0    -- quina pestanya ha demanat el dit, quan `asked` és 'tab'
local again = false    -- la feina sencera s'ha de tornar a fer (les credencials han canviat)
local note = ''        -- l'única línia que la pàgina de Settings té per dir que una cosa no ha anat
local current = { appid = '', lat = '', lon = '' }   -- el que diu el fitxer ara mateix
local painted_ = ''    -- la pàgina que hi ha al vidre: canviar-la vol una passada neta
local gear_from_ = ''  -- la pàgina on era la persona quan va prémer l'engranatge

-- Una caixa diu el que s'està escrivint si n'hi ha, i el que diu el fitxer si no. I el que s'escriu es desa a l'app a cada
-- tecla, perquè la pàgina es tornarà a fer (la banda, un gir) i el control nou no en sap res del vell.
local function value_of(name)
  if typed ~= nil and typed[name] ~= nil then return typed[name] end
  return current[name] or ''
end

local function the_state()
  if state_block ~= nil and not state_block:alive() then state_block = nil end
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function save_state()
  local block = the_state()
  if block == nil then return end
  block:clear()
  -- **El que hi ha a les caixes** i no el que s'ha escrit aquesta estona: una caixa que no s'ha tocat val el que diu el
  -- fitxer, i desar-la buida faria que la pàgina tornés amb un número esborrat que ningú no ha esborrat.
  block:write(0, string.format('layout=%s\npage=%s\nfocus=%s\nappid=%s\nlat=%s\nlon=%s\n', kLayout, page, focus_,
                               value_of('appid'), value_of('lat'), value_of('lon')))
end

local function load_state()
  local block = the_state()
  if block == nil then return end
  local text = block:read(0, block:size()) or ''
  local at = {}
  for line in text:gmatch('[^\r\n]+') do
    local k, v = line:match('^([%w_]+)=(.*)$')
    if k ~= nil then at[k] = v end
  end
  -- Una forma d'una altra versió es llença i la pàgina es fa com si fos el primer cop.
  if at.layout ~= kLayout then return end
  if at.page == 'days' or at.page == 'hours' or at.page == 'settings' then page = at.page end
  if at.focus ~= nil then focus_ = at.focus end
  if at.appid ~= nil or at.lat ~= nil or at.lon ~= nil then
    typed = { appid = at.appid or '', lat = at.lat or '', lon = at.lon or '' }
  end
end

-- ---------------------------------------------------------------- el que s'escriu a la pàgina de Settings

-- El que diu cada caixa i el que val ara: els tres números que l'app demana a la persona.
local kFields = {
  { name = 'appid', says = 'API key (appid) from OpenWeatherMap' },
  { name = 'lat', says = 'Latitude, for example 41.4120' },
  { name = 'lon', says = 'Longitude, for example 2.1483' },
}

local gear, strip, save_button = nil, nil, nil
local lists = {}          -- el que la pàgina del vidre té de llistes (una)
local boxes, captions = {}, {}

local Titles = { 'Now', 'Days', 'Hours' }
local TabOf = { main = 1, days = 2, hours = 3 }
local Pages = { 'main', 'days', 'hours' }

local function let_go()
  if gear ~= nil then gear:release() gear = nil end
  if strip ~= nil then strip:release() strip = nil end
  if save_button ~= nil then save_button:release() save_button = nil end
  for _, one in ipairs(lists) do one:release() end
  for _, one in ipairs(boxes) do if one.box ~= nil then one.box:release() end end
  for _, one in ipairs(captions) do one:release() end
  lists, boxes, captions = {}, {}, {}
end

local function build_gear()
  gear = ui.button(GearW, GearH)
  if gear == nil then return end
  gear.icon = 'settings'
  gear.on_click = function()
    focus_ = 'gear'
    asked = 'gear'
    save_state()
  end
  gear:place(wid - Margin - GearW, Margin)
end

-- **La tira de pestanyes és la del sistema** (0.199.7): tres pàgines —el que fa ara, els dies i les hores— i el dit i el dial
-- les giren com a qualsevol altra app d'aquesta casa (docs/30, docs/45).
local function build_strip()
  strip = ui.tabs(wid - 2 * Margin, TabH)
  if strip == nil then return end
  strip.items = Titles
  strip.index = TabOf[page] or TabOf[painted_] or 1
  strip.on_change = function(self, index)
    focus_ = 'strip'
    asked, asked_tab = 'tab', index
    save_state()
  end
  strip:place(Margin, Margin + GearH + Gap)
end

local function page_top() return Margin + GearH + Gap + TabH + Gap end

-- Una llista de la casa, amb les mides de la pàgina: el peu del paginador ve amb el control. **La lletra de les files és la
-- petita i no la del cos** (0.199.7): una fila té tres llocs d'una línia i el que no hi cap el control ho talla, o sigui que
-- com més petita és la lletra més dades hi entren (docs/30 § `ui.list`).
local function a_list(y, items, lines, separate)
  if y >= hei - Margin then return nil end
  local list = ui.list(wid - 2 * Margin, hei - Margin - y)
  if list == nil then return nil end
  list.items = items
  list.scale = kSmall
  if lines == 2 then
    list.row_height = 2 * ui.line_height(kSmall) + 10
  else
    list.row_height = ui.line_height(kSmall) + 8
  end
  list.separator = (separate == true)
  list:place(Margin, y)
  lists[#lists + 1] = list
  return list
end

-- L'habitació d'una línia de fila: el títol i el subtítol comparteixen el que deixa el detall, i el sistema talla tots dos al
-- mateix lloc. El que es mesura aquí és el que hi cap de debò (docs/30 § `ui.list`).
local function row_room(detail, scale)
  local used = 0
  if detail ~= nil and detail ~= '' then used = ui.text_width(detail, scale) + 12 end
  local room = wid - 2 * Margin - 16 - used
  if room < 0 then room = 0 end
  return room
end

-- ---------------------------------------------------------------- la pàgina

local place, notice, problem = '', '', nil

-- **El JSON es llegeix sense arbre**: the two answers are small and the page wants six numbers and a list of days, and a
-- parser of the whole document would be memory nobody needs (docs/30, docs/75).
local function num(t, k)
  local v = t and t:match('"' .. k .. '"%s*:%s*(-?%d+%.?%d*)')
  return v and tonumber(v) or nil
end

local function word(t, k)
  return t and t:match('"' .. k .. '"%s*:%s*"(.-)"') or nil
end

local function box(t, k)
  -- **Una clau que és un array també es troba** (0.199.4): `"weather":[{"description":…}]` és el que el servei dona, i amb
  -- la clau demanada només com a objecte la descripció del cel no s'escrivia mai.
  local at = t and t:find('"' .. k .. '"%s*:%s*%[?%s*{')
  if at == nil then return nil end
  local from = t:find('{', at)
  local _, stop = t:find('^%b{}', from)
  return stop and t:sub(from, stop) or nil
end

local function degree(v)
  if v == nil then return '--' end
  return string.format('%d%s', math.floor(v + 0.5), DEG)
end

local Months = { 'Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec' }
local Weekdays = { 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun' }
local Where = { 'N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW' }

-- **El primer dia de la setmana és del sistema** (`time.week`): el nom d'un dia no pot dir una cosa i el calendari una altra.
local function weekday_word(wday)
  local first = 1
  if time.week ~= nil then
    local got = time.week()
    if got == 7 then first = 7 end
  end
  return Weekdays[((wday or 1) - first) % 7 + 1] or '?'
end

local function clock_of(epoch)
  if epoch == nil then return '--:--' end
  local d = time.date(epoch)
  return string.format('%02d:%02d', d.hour or 0, d.min or 0)
end

local function day_word(epoch)
  local d = time.date(epoch or 0)
  return string.format('%s %d %s', weekday_word(d.wday), d.day or 0, Months[d.month or 1] or '')
end

local function from_where(deg)
  if deg == nil then return nil end
  return Where[math.floor(((deg % 360) / 45) + 0.5) % 8 + 1]
end

--[[
**UNA PEÇA DE TRES HORES, AMB TOT EL QUE EL SERVEI EN DONA** (0.199.7, l'amo: *«forecast.json te varis dies d'informació, no em
crec que només es diguin els dies i la temperatura. Usa tota la informació»*). Es llegeix una vegada i el que la pàgina no ensenya
no és perquè no hi sigui (docs/30).
]]
local function a_piece(part)
  local mainp = box(part, 'main')
  local windp = box(part, 'wind')
  local cloudsp = box(part, 'clouds')
  local skyp = box(part, 'weather') or ''
  local rainp = box(part, 'rain') or ''
  local snowp = box(part, 'snow') or ''
  local sysp = box(part, 'sys') or ''
  return {
    when = num(part, 'dt'),
    temp = num(mainp, 'temp'), feels = num(mainp, 'feels_like'),
    min = num(mainp, 'temp_min'), max = num(mainp, 'temp_max'),
    humidity = num(mainp, 'humidity'), pressure = num(mainp, 'pressure'),
    sea = num(mainp, 'sea_level'), grnd = num(mainp, 'grnd_level'),
    says = word(skyp, 'description'), sky = word(skyp, 'main'), icon = word(skyp, 'icon'),
    wind = num(windp, 'speed'), deg = num(windp, 'deg'), gust = num(windp, 'gust'),
    clouds = num(cloudsp, 'all'), pop = num(part, 'pop'),
    rain = num(rainp, '3h') or num(rainp, '1h'),
    snow = num(snowp, '3h') or num(snowp, '1h'),
    vis = num(part, 'visibility'), pod = word(sysp, 'pod'),
  }
end

-- Les quaranta peces de tres hores del fitxer, de la primera a l'última.
local function pieces()
  local body = fs.read(FORECAST_JSON) or ''
  local out = {}
  local at = body:find('"list"%s*:%s*%[')
  if at == nil then return out end
  local i = body:find('{', at)
  while i ~= nil do
    local _, stop = body:find('^%b{}', i)
    if stop == nil then break end
    out[#out + 1] = a_piece(body:sub(i, stop))
    i = body:find('{', stop + 1)
  end
  return out
end

--[[
**Els dies surten de les seves peces** (0.199.7): el servei dona trenta-dues hores i mai un resum per dia, i el resum és el que
una persona llegeix. La mínima és la de les peces, la màxima també, la pluja s'hi suma i el cel és el que més s'ha dit —una
mitjana de paraules no vol dir res—, o sigui que cap número de la pàgina és inventat (docs/30).
]]
local function days_of(ps)
  local out, by_day = {}, {}
  for _, p in ipairs(ps) do
    if p.when ~= nil then
      local d = time.date(p.when)
      local key = string.format('%d-%02d-%02d', d.year or 0, d.month or 0, d.day or 0)
      local one = by_day[key]
      if one == nil then
        one = { when = p.when, n = 0, humidity = 0, pressure = 0, clouds = 0, rain = 0, snow = 0,
                feels = 0, n_feels = 0, says = '', said = {}, wind = 0, gust = 0, deg = nil, pop = 0, vis = nil }
        by_day[key] = one
        out[#out + 1] = one
      end
      one.n = one.n + 1
      if p.min ~= nil and (one.min == nil or p.min < one.min) then one.min = p.min end
      if p.max ~= nil and (one.max == nil or p.max > one.max) then one.max = p.max end
      if p.humidity ~= nil then one.humidity = one.humidity + p.humidity end
      if p.feels ~= nil then one.feels = one.feels + p.feels one.n_feels = one.n_feels + 1 end
      if p.pressure ~= nil then one.pressure = one.pressure + p.pressure end
      if p.clouds ~= nil then one.clouds = one.clouds + p.clouds end
      if p.rain ~= nil then one.rain = one.rain + p.rain end
      if p.snow ~= nil then one.snow = one.snow + p.snow end
      if p.pop ~= nil and p.pop > one.pop then one.pop = p.pop end
      if p.wind ~= nil and p.wind > one.wind then one.wind = p.wind one.deg = p.deg end
      if p.gust ~= nil and p.gust > one.gust then one.gust = p.gust end
      if p.vis ~= nil and (one.vis == nil or p.vis < one.vis) then one.vis = p.vis end
      if p.says ~= nil and p.says ~= '' then
        one.said[p.says] = (one.said[p.says] or 0) + 1
        if one.said[p.says] > (one.said[one.says] or 0) then one.says = p.says one.icon = p.icon end
      end
    end
  end
  for _, one in ipairs(out) do
    if one.n > 0 then
      one.humidity = one.humidity / one.n
      one.pressure = one.pressure / one.n
      one.clouds = one.clouds / one.n
    end
    if (one.n_feels or 0) > 0 then one.feels = one.feels / one.n_feels else one.feels = nil end
    one.n_feels = nil
    one.said = nil
  end
  return out
end

-- **El dibuix és el fitxer que el nom de la icona diu** (0.199.9): la resposta porta `icon` —`01d`, `10n`…— i els divuit
-- fitxers amb aquests noms viuen a `res/` de l'app, que és el que `ui.image` i `cv:image` poden llegir (docs/30, docs/70). O
-- sigui que per pintar el cel no cal cap taula: la taula dels codis és per dir-lo amb paraules quan el servei no ho diu.
local kSkins = '/apps/weather/res/'

local function skin_of(icon)
  if icon == nil or icon == '' then return nil end
  return kSkins .. icon .. '.png'
end

-- **El que no hi cap no s'escriu a mitges** (docs/30): the parts are added while they fit, and the line stops at a word and
-- not in the middle of a number.
local function joined(parts, scale, room)
  local out = ''
  for _, one in ipairs(parts) do
    if one ~= nil and one ~= '' then
      local next_one = (out == '') and one or (out .. '  ·  ' .. one)
      if ui.text_width(next_one, scale) > room then return out end
      out = next_one
    end
  end
  return out
end

-- A word that does not fit is carried to the next line and not cut in half (the house rule of a page that says things,
-- docs/30): the same shape every app of this system uses.
local function wrap_at(text, scale, room)
  local lines, line = {}, ''
  for word in tostring(text):gmatch('%S+') do
    local next_line = (line == '') and word or (line .. ' ' .. word)
    if line ~= '' and ui.text_width(next_line, scale) > room then
      lines[#lines + 1] = line
      line = word
    else
      line = next_line
    end
  end
  if line ~= '' then lines[#lines + 1] = line end
  return lines
end

--[[
LA PÀGINA DEL QUE FA ARA (0.199.7): el lloc i de quan és a dalt, el grau gran, i **totes les dades de `now.json` en una llista**
del sistema —el que el servei no ha enviat no s'inventa i la seva fila no surt— (l'amo: *«now.json no te gaire chicha... pero
forecast.json te varis dies d'informació»*, i la llista és el que fa que el que hi ha s'hi vegi).
]]
local function paint_now()
  local y = page_top()
  local room = wid - 2 * Margin - GearW - 12

  local function say_lines(text, scale, gap)
    for _, line in ipairs(wrap_at(text, scale, room)) do
      if y + ui.line_height(scale) > hei - Margin then return false end
      ui.text(Margin, y, line, scale, INK)
      y = y + ui.line_height(scale) + gap
    end
    return true
  end

  -- **El títol és el lloc**, i **sota, més petit, de quan és el que es llegeix**: una dada vella dita com a vella serveix
  -- (docs/30, docs/70). Sense fitxer encara no hi ha cap lloc i el que mana és el motiu.
  if place ~= '' then
    say_lines(place, kTitle, 4)
    say_lines(notice, kSmall, 4)
  else
    say_lines(notice, kBody, 6)
  end
  if problem ~= nil and #problem > 0 then
    if say_lines(problem, kSmall, 2) then y = y + 2 end
  end

  local now = fs.read(NOW_JSON) or ''
  local main = box(now, 'main')
  local skyp = box(now, 'weather') or ''
  local windp = box(now, 'wind') or ''
  local cloudsp = box(now, 'clouds') or ''
  local sysp = box(now, 'sys') or ''
  local rainp = box(now, 'rain') or ''
  local snowp = box(now, 'snow') or ''
  local coordp = box(now, 'coord') or ''
  local temp = num(main, 'temp')

  if temp ~= nil then
    local big = degree(temp)
    -- **El cel, dibuixat** (0.199.9): la icona de la resposta al costat del grau gran, tan alta com la línia del grau. `mask`
    -- deixa el paper on el PNG és transparent, que és el que aquests dibuixos tenen a les cantonades (docs/30).
    local left = Margin
    local skin = skin_of(word(skyp, 'icon'))
    local side = ui.line_height(kBig) + 6
    if skin ~= nil and ui.image(Margin, y + 4, skin, side, side, nil, true) then left = Margin + side + 14 end

    ui.text(left, y, big, kBig, INK)
    local says = word(skyp, 'description') or ''
    if says ~= '' then
      local room_says = wid - Margin - GearW - 12 - (left + ui.text_width(big, kBig) + 20)
      local first = wrap_at(says, kBody, room_says)[1]
      if first ~= nil then
        ui.text(left + ui.text_width(big, kBig) + 20, y + ui.line_height(kBig) - ui.line_height(kBody), first, kBody, INK)
      end
    end
    y = y + ui.line_height(kBig) + 4
  end

  --[[
  **Cada número que el servei ha enviat, amb el seu nom** (0.199.7): una fila de la llista té tres llocs —el nom, el número i
  una línia de sota per al que no hi cap— i el que el servei no ha enviat no s'inventa: la seva fila no surt (docs/30).
  ]]
  local facts = {}
  local room_line = wid - 2 * Margin - 24
  local function fact(name, detail, extra)
    if (detail ~= nil and detail ~= '') or (extra ~= nil and extra ~= '') then
      facts[#facts + 1] = { title = name, detail = detail or '', subtitle = extra }
    end
  end

  if num(main, 'feels_like') ~= nil then fact('Feels like', degree(num(main, 'feels_like'))) end
  if num(main, 'temp_min') ~= nil then
    fact('Low and high', degree(num(main, 'temp_min')) .. ' / ' .. degree(num(main, 'temp_max')))
  end
  fact('Sky', word(skyp, 'main'), word(skyp, 'description'))
  if num(main, 'humidity') ~= nil then fact('Humidity', string.format('%d%%', math.floor(num(main, 'humidity') + 0.5))) end
  if num(main, 'pressure') ~= nil then
    local extra = nil
    if num(main, 'sea_level') ~= nil or num(main, 'grnd_level') ~= nil then
      local parts = {}
      if num(main, 'sea_level') ~= nil then parts[#parts + 1] = string.format('sea %d', math.floor(num(main, 'sea_level') + 0.5)) end
      if num(main, 'grnd_level') ~= nil then parts[#parts + 1] = string.format('ground %d', math.floor(num(main, 'grnd_level') + 0.5)) end
      extra = joined(parts, kSmall, room_line)
    end
    fact('Pressure', string.format('%d hPa', math.floor(num(main, 'pressure') + 0.5)), extra)
  end
  if num(windp, 'speed') ~= nil then
    local said = string.format('%.1f m/s', num(windp, 'speed'))
    if from_where(num(windp, 'deg')) ~= nil then said = said .. ' ' .. from_where(num(windp, 'deg')) end
    fact('Wind', said, (num(windp, 'gust') ~= nil) and string.format('gusts %.1f m/s', num(windp, 'gust')) or nil)
  end
  if num(cloudsp, 'all') ~= nil then fact('Clouds', string.format('%d%%', math.floor(num(cloudsp, 'all') + 0.5))) end
  if num(now, 'visibility') ~= nil then fact('Visibility', string.format('%.1f km', num(now, 'visibility') / 1000)) end
  if num(rainp, '1h') ~= nil then fact('Rain, an hour', string.format('%.1f mm', num(rainp, '1h'))) end
  if num(rainp, '3h') ~= nil then fact('Rain, 3 hours', string.format('%.1f mm', num(rainp, '3h'))) end
  if num(snowp, '1h') ~= nil then fact('Snow, an hour', string.format('%.1f mm', num(snowp, '1h'))) end
  if num(snowp, '3h') ~= nil then fact('Snow, 3 hours', string.format('%.1f mm', num(snowp, '3h'))) end
  if num(sysp, 'sunrise') ~= nil then
    fact('Sun', clock_of(num(sysp, 'sunrise')) .. ' / ' .. clock_of(num(sysp, 'sunset')))
  end
  if num(coordp, 'lat') ~= nil then
    fact('Coordinates', nil, string.format('%.4f, %.4f', num(coordp, 'lat'), num(coordp, 'lon')))
  end
  if word(now, 'name') ~= nil then
    local said = word(now, 'name')
    if word(sysp, 'country') ~= nil then said = said .. ', ' .. word(sysp, 'country') end
    fact('Where', said)
  end
  if num(now, 'dt') ~= nil then fact('The answer', clock_of(num(now, 'dt')), day_word(num(now, 'dt'))) end

  if #facts == 0 then
    ui.text(Margin, y, 'the file has nothing to say', kBody, INK)
    return
  end

  local list = a_list(y, facts, 2, true)
  if list ~= nil then list.on_focus = function(self, on) if on then focus_ = 'facts' end end end
end

--[[
ELS DIES (0.199.7): **el resum de cada dia surt de les seves peces i no s'inventa** —la mínima de les peces, la màxima, la pluja
sumada, el vent més fort amb la seva direcció i el cel que més s'ha dit— i la fila en duu tot el que el servei dona d'aquell dia
(docs/30).
]]
--[[
ELS DIES (0.199.9): **cada dia és una fila feta per l'app** —una entrada de la llista amb el dibuix del cel a l'esquerra i tres
línies de números— perquè una fila que el sistema dibuixa a partir de tres textos no pot portar cap imatge, i el cel dibuixat és
el que una app del temps ha d'ensenyar. Els números surten de les peces del dia i no s'inventen: la mínima i la màxima de les
peces, la pluja i la neu sumades, la humitat, la pressió, els núvols i la visibilitat mitjanes, la ratxa més forta i **el cel que
més s'ha dit** —una mitjana de paraules no vol dir res— (docs/30, docs/45).
]]
local function paint_days()
  local y = page_top()
  local ds = days_of(pieces())

  if #ds == 0 then
    ui.text(Margin, y, 'the forecast file has no days in it', kBody, INK)
    return
  end
  if y >= hei - Margin then return end

  local entry_w = wid - 2 * Margin
  -- **Quatre línies i el dibuix a l'esquerra, i les cinc files hi caben en una pàgina** (0.199.9): 4 × 36 + 4 = 148 i la
  -- pàgina en té 741, o sigui que el paginador no apareix i cinc dies es llegeixen d'una tirada. El nom del dia va en
  -- majúscules perquè la línia es llegeixi com un capçal sense haver de ser més gran que els números (docs/30, docs/90).
  local row_h = 4 * ui.line_height(kSmall) + 4
  -- La icona no es menja el text: seixanta-quatre píxels deixen trenta-tres caràcters per línia, i el dibuix
  -- s'aguanta a aquesta mida en aquesta pàgina.
  local icon_px = 64

  local list = ui.list(entry_w, hei - Margin - y)
  if list == nil then return end
  list.scale = kSmall
  list.row_height = row_h
  list.separator = true
  list:place(Margin, y)
  lists[#lists + 1] = list

  for _, d in ipairs(ds) do
    local entry = ui.list_entry(entry_w, row_h)
    if entry == nil then break end

    local skin = skin_of(d.icon)
    local picture = nil
    if skin ~= nil then
      picture = ui.canvas(icon_px, icon_px)
      if picture ~= nil then
        picture:place(6, (row_h - icon_px) // 2)
        picture.on_draw = function(self)
          self:clear(PAPER)
          self:image(0, 0, skin, icon_px, icon_px)
        end
        entry:add(picture)
      end
    end

    local text_x = (picture ~= nil) and (6 + icon_px + 10) or 8
    local text_w = entry_w - text_x - 12
    local line_h = ui.line_height(kSmall)

    -- **El capçal del dia amb la mínima i la màxima a la dreta**, i tres línies de números a sota.
    local temps = degree(d.min) .. ' / ' .. degree(d.max)
    local head_lbl = ui.label(text_w - ui.text_width(temps, kSmall) - 8, line_h)
    if head_lbl ~= nil then
      local head = string.upper(day_word(d.when))
      if d.says ~= nil and d.says ~= '' then head = head .. '  ·  ' .. d.says end
      head_lbl.text = head
      head_lbl.scale = kSmall
      head_lbl.color = INK
      head_lbl:place(text_x, 2)
      entry:add(head_lbl)
    end
    local temps_lbl = ui.label(ui.text_width(temps, kSmall) + 4, line_h)
    if temps_lbl ~= nil then
      temps_lbl.text = temps
      temps_lbl.scale = kSmall
      temps_lbl.color = INK
      temps_lbl:place(entry_w - 12 - ui.text_width(temps, kSmall), 2)
      entry:add(temps_lbl)
    end

    local under = {}
    if d.feels ~= nil then under[#under + 1] = 'feels ' .. degree(d.feels) end
    if d.pop ~= nil and d.pop > 0 then under[#under + 1] = string.format('rain %d%%', math.floor(d.pop * 100 + 0.5)) end
    if d.humidity ~= nil and d.humidity > 0 then under[#under + 1] = string.format('hum %d%%', math.floor(d.humidity + 0.5)) end
    local second = ui.label(text_w, line_h)
    if second ~= nil then
      second.text = joined(under, kSmall, text_w)
      second.scale = kSmall
      second.color = INK
      second:place(text_x, 2 + line_h + 1)
      entry:add(second)
    end

    local more = {}
    if d.wind ~= nil and d.wind > 0 then
      local said = string.format('wind %.1f m/s', d.wind)
      if from_where(d.deg) ~= nil then said = said .. ' ' .. from_where(d.deg) end
      if d.gust ~= nil and d.gust > 0 then said = said .. string.format(' · gusts %.1f', d.gust) end
      more[#more + 1] = said
    end
    local third = ui.label(text_w, line_h)
    if third ~= nil then
      third.text = joined(more, kSmall, text_w)
      third.scale = kSmall
      third.color = INK
      third:place(text_x, 2 + 2 * (line_h + 1))
      entry:add(third)
    end

    local last = {}
    if d.rain ~= nil and d.rain > 0 then last[#last + 1] = string.format('%.1f mm', d.rain) end
    if d.snow ~= nil and d.snow > 0 then last[#last + 1] = string.format('snow %.1f mm', d.snow) end
    if d.pressure ~= nil and d.pressure > 0 then last[#last + 1] = string.format('%.0f hPa', d.pressure) end
    if d.clouds ~= nil and d.clouds > 0 then last[#last + 1] = string.format('clouds %d%%', math.floor(d.clouds + 0.5)) end
    if d.vis ~= nil and d.vis > 0 then last[#last + 1] = string.format('%.0f km of view', d.vis / 1000) end
    local fourth = ui.label(text_w, line_h)
    if fourth ~= nil then
      fourth.text = joined(last, kSmall, text_w)
      fourth.scale = kSmall
      fourth.color = INK
      fourth:place(text_x, 2 + 3 * (line_h + 1))
      entry:add(fourth)
    end

    list:add(entry)
  end

  list.on_focus = function(self, on) if on then focus_ = 'days' end end
end

--[[
LES HORES (0.199.7): les quaranta peces de tres hores, amb tot el que el servei en dona a cada una —el que fa i el que se sent,
el cel, el vent amb la seva direcció i les ratxes, la humitat, la pressió, els núvols, la probabilitat i els mil·límetres de pluja
o de neu, la visibilitat i si és de dia o de nit— i **cada peça són dues files**: una fila de tres llocs no és capaç de dir
setze números, i una pàgina que els amaga no és més completa (docs/30).
]]
local function paint_hours()
  local y = page_top()
  local ps = pieces()

  if #ps == 0 then
    ui.text(Margin, y, 'the forecast file has no hours in it', kBody, INK)
    return
  end

  local items = {}
  for _, p in ipairs(ps) do
    local temps = degree(p.temp) .. ' / ' .. degree(p.feels)
    local when = clock_of(p.when) .. '  ' .. (p.says or '')
    if p.pod == 'n' then when = when .. '  night' elseif p.pod == 'd' then when = when .. '  day' end
    items[#items + 1] = { title = when, detail = temps }

    -- **El que compta primer és el que no es pot perdre si la fila es queda curta** (0.199.7): `joined` deixa caure les
    -- parts del final, o sigui que el vent i la humitat van davant dels núvols i de la visibilitat (docs/30).
    local two = {}
    if p.wind ~= nil then
      local said = string.format('wind %.1f m/s', p.wind)
      if from_where(p.deg) ~= nil then said = said .. ' ' .. from_where(p.deg) end
      if p.gust ~= nil then said = said .. string.format(' · gusts %.1f', p.gust) end
      two[#two + 1] = said
    end
    items[#items + 1] = { title = joined(two, kSmall, row_room('', kSmall)) }

    local three = {}
    if p.humidity ~= nil then three[#three + 1] = string.format('hum %d%%', math.floor(p.humidity + 0.5)) end
    if p.pressure ~= nil then three[#three + 1] = string.format('%.0f hPa', p.pressure) end
    if p.min ~= nil then three[#three + 1] = degree(p.min) .. ' to ' .. degree(p.max) end
    items[#items + 1] = { title = joined(three, kSmall, row_room('', kSmall)) }

    local four = {}
    if p.clouds ~= nil then four[#four + 1] = string.format('clouds %d%%', math.floor(p.clouds + 0.5)) end
    if p.pop ~= nil and p.pop > 0 then four[#four + 1] = string.format('rain %d%%', math.floor(p.pop * 100 + 0.5)) end
    if p.rain ~= nil and p.rain > 0 then four[#four + 1] = string.format('%.1f mm', p.rain) end
    if p.snow ~= nil and p.snow > 0 then four[#four + 1] = string.format('snow %.1f mm', p.snow) end
    if p.vis ~= nil then four[#four + 1] = string.format('%.0f km of view', p.vis / 1000) end
    items[#items + 1] = { title = joined(four, kSmall, row_room('', kSmall)) }
  end

  local list = a_list(y, items, 1, false)
  if list ~= nil then list.on_focus = function(self, on) if on then focus_ = 'hours' end end end
end

-- **La pàgina on s'escriuen la clau i el lloc** (0.199.4): tres caixes, una per cosa, i el botó que les desa. És l'única porta
-- que té una placa sense credencials —o amb una clau que el servei refusa— per deixar de no tenir-ne, i és la que falta quan
-- l'app es queixa i no deixa fer res més (l'amo: *«Quan l'app falla no deixa accedir a Settings de l'app»*).
local function paint_settings()
  local room = wid - 2 * Margin - GearW - 12
  local y = page_top()

  ui.text(Margin, y, 'Settings', kTitle, INK)
  y = y + ui.line_height(kTitle) + 4
  for _, line in ipairs(wrap_at('The key and the place of the forecast, kept in ' .. CONFIG .. '.', kSmall, room)) do
    ui.text(Margin, y, line, kSmall, INK)
    y = y + ui.line_height(kSmall) + 2
  end
  y = y + 6

  for _, what in ipairs(kFields) do
    local caption = ui.label(room, ui.line_height(kSmall))
    if caption ~= nil then
      caption.text = what.says
      caption.scale = kSmall
      caption:place(Margin, y)
      captions[#captions + 1] = caption
    end
    y = y + ui.line_height(kSmall) + 4

    local field = ui.textbox(room, 60)
    if field ~= nil then
      local name = what.name
      field.text = value_of(name)
      field.scale = kBody
      field.on_change = function(self, text)
        typed = typed or {}
        typed[name] = tostring(text or '')
      end
      field.on_focus = function(self, on) if on then focus_ = name end end
      field:place(Margin, y)
      boxes[#boxes + 1] = { name = name, box = field }
    end
    y = y + 60 + 14
  end

  save_button = ui.button(room, ui.line_height(kBody) + 20)
  if save_button ~= nil then
    save_button.title = 'Save'
    save_button.scale = kBody
    save_button.primary = true
    save_button.on_click = function()
      focus_ = 'save'
      asked = 'save'
      save_state()
    end
    save_button:place(Margin, y)
    y = y + ui.line_height(kBody) + 20 + 10
  end

  -- **El que no s'ha pogut desar es diu aquí i no a la línia del temps**: la pàgina que ha de canviar alguna cosa és la que
  -- n'ha de dir el motiu.
  if #note > 0 then
    for _, line in ipairs(wrap_at(note, kSmall, wid - 2 * Margin)) do
      if y + ui.line_height(kSmall) > hei - Margin then break end
      ui.text(Margin, y, line, kSmall, INK)
      y = y + ui.line_height(kSmall) + 2
    end
  end
end

local function paint()
  if not has_glass then return end
  let_go()
  ui.clear(PAPER)
  build_gear()
  build_strip()

  if page == 'settings' then
    paint_settings()
  elseif page == 'days' then
    paint_days()
  elseif page == 'hours' then
    paint_hours()
  else
    paint_now()
  end

  -- El cercle torna on era: la pàgina es torna a fer a cada tecla de la banda —que canvia l'alçada de la vista— i el dial no
  -- ha de començar la volta pel primer control (docs/30, docs/45).
  local target = nil
  if focus_ == 'gear' then
    target = gear
  elseif focus_ == 'strip' then
    target = strip
  elseif focus_ == 'save' then
    target = save_button
  elseif focus_ == 'facts' or focus_ == 'days' or focus_ == 'hours' then
    target = lists[1]
  else
    for _, one in ipairs(boxes) do if one.name == focus_ then target = one.box end end
  end
  if target ~= nil then ui.focus(target) end

  -- **Una pàgina nova es pinta sencera i la mateixa només amb la passada ràpida**: canviar de pestanya deixa píxels de l'altra
  -- a sota, i una descàrrega que arriba no ha de costar una passada neta de tres-cents mil·lisseconds (docs/30, docs/50).
  ui.present((page ~= painted_) and 'gc16' or 'gld16')
  painted_ = page
  save_state()
end

function on_resize(w, h)
  wid, hei = w, h
  paint()
end

-- ---------------------------------------------------------------- 1. les credencials

-- **De quan és el que es llegeix**, en la paraula que ho diu sense fer-hi pensar: el fitxer més vell dels dos és el que
-- mana, perquè una pàgina no pot ser més nova que la seva meitat més vella.
local function age_line(now_age, forecast_age)
  if now_age == nil or forecast_age == nil then return 'of an age this disk does not keep' end
  local older = math.max(now_age, forecast_age)
  if older < 120 then return 'from just now' end
  if older < 3600 then return string.format('from %d min ago', math.floor(older / 60)) end
  return string.format('from %d h ago', math.floor(older / 3600 + 0.5))
end

-- L'edat en segons, o `nil` quan no es pot saber: el fitxer no hi és, el disc no en guarda l'hora, o el rellotge de la
-- placa encara no sap quina hora és. Tots tres casos volen dir el mateix per a aquesta app: no es pot provar que la
-- resposta sigui d'avui.
local function age_of(path)
  local info = fs.stat(path)
  if info == nil or info.type ~= 'file' then return nil end
  local when = tonumber(info.mtime) or 0
  if when <= 0 then return nil end
  local now = time.now()
  if now == nil or now <= 0 then return nil end
  return now - when
end

local function the_work()
  if CONFIG == nil then
    say('this app has no ground on this board')
    place = ''
    notice = 'Nowhere to keep it'
    problem = 'This app has no ground on this board, so there is nothing to download into.'
    paint()
    return
  end

  local text = fs.read(CONFIG)
  local cfg = {}
  if type(text) == 'string' then
    for line in text:gmatch('[^\r\n]+') do
      local k, v = line:match('^%s*([%w_]+)%s*=%s*(.-)%s*$')
      if k ~= nil then cfg[k:lower()] = v end
    end
  end
  -- El que diu el fitxer es queda a `current`: és el que la pàgina de Settings ensenya a les seves caixes quan no s'hi ha
  -- escrit res encara.
  current = { appid = cfg.appid or '', lat = cfg.lat or '', lon = cfg.lon or '' }
  local key, lat, lon = current.appid, tonumber(current.lat), tonumber(current.lon)
  if #key == 0 or lat == nil or lon == nil then
    say(string.format('no credentials in %s: the gear is the way in', CONFIG))
    place = ''
    notice = 'No credentials'
    -- **La porta és l'engranatge** (0.199.4): l'app no demana un editor de text, deixa escriure la clau i el lloc aquí.
    problem = 'Tap the gear and type the appid, the latitude and the longitude. ' ..
              'Nothing is downloaded until the three are there.'
    paint()
    return
  end

  local now_age, forecast_age = age_of(NOW_JSON), age_of(FORECAST_JSON)
  local fresh = now_age ~= nil and forecast_age ~= nil and now_age < kDay and forecast_age < kDay
  local downloaded = false

  if fresh then
    say('the two answers are younger than 24 h: nothing is downloaded')
  else
    say(string.format('downloading the two answers (now.json %s, forecast.json %s)',
                      now_age == nil and 'not there or undated' or 'older than 24 h',
                      forecast_age == nil and 'not there or undated' or 'older than 24 h'))
    -- El lloc que ja es té es queda a dalt mentre es baixa: la pàgina no s'ha de buidar per anar a buscar dues respostes.
    place = word(fs.read(NOW_JSON) or '', 'name') or ''
    notice = 'Downloading'
    problem = nil
    paint()

    local function an_address(base)
      return string.format('%s?lat=%.4f&lon=%.4f&units=metric&lang=en&appid=%s', base, lat, lon, key)
    end

    -- Una resposta que no arriba sencera es diu i no es dona per bona. **I la falta de memòria crida**: és el que l'amo va
    -- demanar amb totes les paraules, i una línia en majúscules no es pot confondre amb un servei que ha dit que no
    -- (docs/30, docs/75).
    local function fetch(base, path)
      local ok, answer = pcall(function() return http.download(an_address(base), path, kTimeoutMs):await() end)
      if not ok then
        local code = (type(answer) == 'table') and answer.code or tostring(answer)
        if tostring(code):find('SPC') or tostring(code):find('MEM') then
          say('*** OUT OF MEMORY *** the board had no RAM downloading ' .. path ..
              ': close another app and open this one again')
          problem = 'OUT OF MEMORY while downloading: close another app and open this one again'
        else
          say(path .. ': ' .. tostring(code))
          problem = tostring(code)
        end
        return false
      end
      local status = (type(answer) == 'table') and (answer.status or 0) or 0
      if status ~= 200 then
        say(string.format('%s: the service said %d', path, status))
        problem = string.format('the service said %d', status)
        return false
      end
      local bytes = (type(answer) == 'table') and (answer.bytes or 0) or 0
      say(string.format('%s: %d, %.1f KB', path, status, bytes / 1024))
      return true
    end

    local now_ok = fetch('https://api.openweathermap.org/data/2.5/weather', NOW_JSON)
    local forecast_ok = fetch('https://api.openweathermap.org/data/2.5/forecast', FORECAST_JSON)
    downloaded = now_ok and forecast_ok
    if downloaded then say('both answers are here: the next one is in 24 h') end
    -- Les edats es tornen a preguntar sempre: el que ha arribat té l'hora d'ara i el que ha fallat segueix essent el vell.
    now_age, forecast_age = age_of(NOW_JSON), age_of(FORECAST_JSON)
  end

  -- **El títol i l'edat, amb els fitxers que hi ha al disc ara mateix**: el lloc surt del `name` de la resposta d'ara i
  -- l'avís de l'hora del fitxer més vell dels dos, tant si s'acaba de baixar com si fa vint hores que hi és.
  place = word(fs.read(NOW_JSON) or '', 'name') or ''
  notice = 'Data ' .. age_line(now_age, forecast_age)
  paint()
end

--[[
**EL QUE HA DEMANAT UN GANXO ES FA AQUÍ** (0.199.4): un `on_click` corre amb la coroutine de l'app aparcada, i a més el botó
que s'ha premut és un dels controls que la pàgina nova ha de tornar —alliberar-lo mentre el sistema el té a la mà és demanar
que la pàgina es desfaci sota els peus—. O sigui que el ganxo deixa un mot i la coroutine, que és qui pot esperar la xarxa i
qui pinta, fa la feina (docs/30).
]]
local function save_the_credentials()
  local function of(name)
    for _, one in ipairs(boxes) do
      if one.name == name and one.box ~= nil then
        return (tostring(one.box.text or ''):gsub('^%s+', ''):gsub('%s+$', ''))
      end
    end
    return tostring((typed and typed[name]) or '')
  end

  local appid, lat, lon = of('appid'), of('lat'), of('lon')
  local alat, alon = tonumber(lat), tonumber(lon)

  local why = nil
  if #appid == 0 then
    why = 'The appid is empty: the key is the first thing the service asks for.'
  elseif alat == nil or alat < -90 or alat > 90 then
    why = 'The latitude is not a number between -90 and 90.'
  elseif alon == nil or alon < -180 or alon > 180 then
    why = 'The longitude is not a number between -180 and 180.'
  end
  if why ~= nil then
    note = why
    say(why)
    paint()
    return
  end

  local wrote = fs.write(CONFIG, string.format('appid=%s\nlat=%s\nlon=%s\n', appid, lat, lon))
  if wrote == nil then
    note = 'The file could not be written: is the card full?'
    say(note)
    paint()
    return
  end

  -- El que s'ha escrit és el que val: les caixes tornen a començar del fitxer i la feina sencera es fa un altre cop (la clau
  -- nova pot fer que les dues respostes que hi ha siguin d'un altre lloc, i això es veu baixant-les). **I el que deia la
  -- pàgina d'abans no es queda**: el motiu d'una fallada que ja no hi és seria una mentida en un lloc nou.
  typed, note, focus_ = nil, '', ''
  place, notice, problem = '', 'Downloading', nil
  page = 'main'
  again = true
  say(string.format('the credentials are written in %s: asking again', CONFIG))
  paint()
  save_state()
end

load_state()
the_work()

-- **Els ganxos de la vida de l'app es defineixen abans del bucle** (0.199.4): el bucle no s'acaba mai, o sigui que una funció
-- que es declari després no existeix mai i la forma de la vista no es desaria en cap pausa.
function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

-- **La pàgina és de l'app, i una app que s'acaba torna el vidre** (`ui::give_back_the_glass`): quan el guió s'acaba, la pàgina
-- s'esborra i el que la persona estava llegint ja no hi és. Per això la coroutine es queda viva sense fer res: `sys.delay` torna
-- el torn, i el sistema continua pintant i rebent el dit. La pàgina la tanca el sistema —la creu de la barra o el pessic—, que
-- acaba l'app tant si és aquí com si no (docs/30, docs/80). **I una placa sense vidre no té res a guardar**, o sigui que allà el
-- bucle no corre i el registre és tot el que deixa.
while has_glass do
  sys.delay(500):await()

  if asked == 'gear' then
    asked = nil
    if page == 'settings' then
      page = (TabOf[gear_from_] ~= nil) and gear_from_ or 'main'
      note = ''
    else
      gear_from_ = page
      page = 'settings'
    end
    paint()
    save_state()
  elseif asked == 'tab' then
    asked = nil
    local which = Pages[asked_tab]
    if which ~= nil then
      page = which
      focus_ = 'strip'
      paint()
      save_state()
    end
  elseif asked == 'save' then
    asked = nil
    save_the_credentials()
  end

  if again then
    again = false
    the_work()
  end
end
