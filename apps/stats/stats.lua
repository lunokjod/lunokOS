-- stats: **el que costa la màquina, amb els pastissos i les gràfiques de l'antic** (0.192.36, l'amo: *«No s'assembla una merda a aquella
-- app... t'he dit igual... amb les gráfiques, els pastísos... les stats ordenades.... Esta molt xulo el que has fet. Però no te res a
-- veure!»*).
--
-- L'original és `OLD/PaperOS/src/apps/log_app.cpp` —una app **en C++ del nucli**, amb quatre vistes i un botó que les recorre—: el
-- **resum** amb els comptes per categoria, una **gràfica** i les repeticions; l'**energia**, que és un **pastís** de quatre porcions amb
-- la seva llegenda; els **dispositius**; i el **registre** sencer. Aquesta pàgina en fa les tres primeres amb els comptadors que aquest
-- nucli ja mesura:
--
--   · **Cost**: les estadístiques **ordenades pel que costen** —una barra fosca per comptador, el nom a l'esquerra i el total a la
--     dreta, que és el dibuix del resum de l'antic—;
--   · **Time**: el **pastís** de quatre porcions amb la **llegenda** al costat: el vidre, l'emmagatzematge, Lua i la resta del que s'ha
--     mesurat, amb una barra, el temps i el percentatge de cada un. Els tons de la llegenda són els del control (`color`, `+4`, `+8`,
--     `+12`), o sigui que la quadrícula i la porció són el mateix to (docs/30);
--   · **Load**: dues gràfiques que es refresquen cada deu segons mentre la pestanya és al vidre —la càrrega del sistema
--     (`power.load()`, la mateixa mesura que imprimeix `load` al prompt) i la memòria en ús (`sys.heap()`)—, que és el que va
--     demanar l'amo: *«A l'app d'Stats: afegir una pestanya nova: "Load", i anar monitoritzant la cárrega del sistema (comanda
--     load) fent una gráfica. I assota unaltra gràfica amb la quantitat de memoria en us actualment (refresc de les gráfiques
--     temporitzat mentre la vista estigui activa, cada 10 segons)»* (0.192.48). **I la finestra és de mitja hora** (0.192.60,
--     l'amo: *«Que les gráfiques de la pestanya Load de l'app Stats pugui cabre historial de 30 minuts enrrere, ara la gráfica
--     apenes aguanta 7 minuts»*), amb la història en una sessió pròpia que es torna a trobar en entrar-hi
--     (*«Usa session state per guardar l'historial entre sortir i tornar a l'app»*) i un **`tick`** del sistema que en pren una
--     cada minut mentre la placa és desperta;
--   · **Memory**: `sys.heap()` —la RAM, el bloc més gran, la memòria extra i el cim de Lua—;
--   · **Info**: el que l'`info_app` ensenyava —placa, versió, uptime, bateria, ràdio—.
--
-- **El que NO fa**: no reseteja cap comptador ni esborra res. Una pàgina de diagnòstic que toca el que mesura menteix la propera vegada.
--
-- **I la gràfica de línies de l'antic no la pot fer cap pàgina sobre comptadors**: aquella era el **ritme de línies del registre**
-- («lines per 2 s») i aquest sistema no guarda cap història —`sys.stats` dona el total d'un comptador i prou—. Una ratlla inventada
-- sobre dades que no existeixen és una mentida amb bona pinta. **El que sí que es pot mostrejar és el present** (0.192.48): la
-- càrrega i la memòria es pregunten cada deu segons, la finestra és d'aquesta app i el que hi ha a la gràfica és el que s'ha mesurat.

local caps = sys.caps()
if not caps.display then
  console.print('stats: this board has no glass')
  return
end

-- **`v4`**: la pestanya **Load** entra a la tira i mou les de Memory i Info un lloc (0.192.48), o sigui que una forma de la `v1`,
-- la `v2` o la `v3` es llença: un `tab=3` d'abans seria ara la pestanya nova (docs/30, docs/45).
local kLayout = 'v4'
local kStateBytes = 1024

-- **La lletra d'aquesta pàgina és gran i fosca** (0.192.66, l'amo: *«A tot l'app Stats, excepte les pestanyes. Text mes gran en
-- genera, i no usis grisos claret»*): the body of the glass is thirty-six pixels (four steps), o sigui que una fila de tres
-- línies a dos passos —divuit— era mitja pàgina de text que no es llegeix. El cos de la lletra d'aquí és **tres passos** (vint-i-set
-- píxels) a tot arreu, i el to del detall és el gris fosc de la casa i no el clar, que sobre aquest paper no es veu (docs/30, docs/92).
local MARGIN = 16
local GAP = 10
local TITLE_H = 52
local SUB_H = 34
local TABS_H = 46
local PIE_SIDE = 244   -- el costat del pastís; el radi del vidre es talla a 120, o sigui que aquest és el màxim

-- **L'ordre de la tira és l'ordre de les pestanyes** i els seus números són els que `shape.tab` desa (docs/30).
local kCostTab, kTimeTab, kLoadTab, kMemoryTab, kInfoTab = 1, 2, 3, 4, 5
local TABS = { 'Cost', 'Time', 'Load', 'Memory', 'Info' }

local state_block = nil
-- **`shape` abans de les funcions que el llegeixen**: un `local` només existeix per a les funcions que es defineixen després.
local shape = { tab = 1, page = 1 }

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

local function save_state()
  local block = the_state()
  if block == nil then return false end
  block:clear()
  block:write(0, string.format('%s\ntab=%d\npage=%d\n', kLayout, shape.tab or 1, shape.page or 1))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return end
  local text = block:read(0, kStateBytes) or ''
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  local head = text:match('^([^\n]*)')
  if head ~= kLayout then return end
  local tab = tonumber(text:match('tab=(%-?%d+)'))
  local page = tonumber(text:match('page=(%-?%d+)'))
  if tab ~= nil and tab >= 1 and tab <= #TABS then shape.tab = math.floor(tab) end
  if page ~= nil and page >= 1 then shape.page = math.floor(page) end
end

-- ------------------------------------------------------------------ els números, dits com els diu el prompt

-- **La mateixa escala que `meter::human_us`**: `845us`, `12.3ms`, `1.98s`, `3 min 07 s`, `1 h 12 min`. És la lletra de la columna del
-- prompt, i una pàgina que digués el mateix número d'una altra manera seria una segona veritat (docs/30).
local function human(us)
  if us == nil then return '-' end
  if us < 1000 then return string.format('%dus', math.floor(us)) end
  local ms = us / 1000
  if ms < 1000 then return string.format('%.1fms', ms) end
  local s = ms / 1000
  if s < 60 then return string.format('%.2fs', s) end
  local minutes = math.floor(s / 60)
  local rest = math.floor(s) % 60
  if minutes < 60 then return string.format('%d min %02d s', minutes, rest) end
  return string.format('%dh %02dmin', math.floor(minutes / 60), minutes % 60)
end

local function kb(bytes)
  if bytes == nil then return '-' end
  if bytes >= 1024 * 1024 then return string.format('%.1f MB', bytes / (1024 * 1024)) end
  return string.format('%.1f KB', bytes / 1024)
end

-- **I una fila pot portar la seva barra de %** (0.192.67, l'amo: *«Al usar llistes hem perdut la barra de %? no podem anidar-la?»*):
-- a row of a list is an entry the app fills (`ui.list_entry` + `:add`), o sigui que el que hi ha dins pot ser el que la pàgina vulgui:
-- el títol, el número i **una barra del sistema** (`ui.progress`). `bar` és el percentatge, o `nil` quan la fila no té un total amb
-- què comparar-se (docs/30, docs/90).
local function row(title, detail, subtitle, bar)
  return { title = title, detail = detail, subtitle = subtitle, bar = bar }
end

local function percent_of(part, whole)
  if part == nil or whole == nil or whole <= 0 then return nil end
  local pct = math.floor(part * 100 / whole + 0.5)
  if pct < 0 then pct = 0 end
  if pct > 100 then pct = 100 end
  return pct
end

-- ------------------------------------------------------------------ les quatre fulles

-- **Els comptadors que contenen els altres no compten al pastís**: `kernel.loop` és el torn sencer i `queue.wait` i `lua.own` són
-- trossos d'ell, o sigui que sumar-los tots diria el temps dues vegades. Es treuen pel nom, que és el que la taula del prompt diu (docs/30).
local kNotAFamily = { ['kernel.loop'] = true, ['queue.wait'] = true, ['lua.own'] = true }

-- **Les quatre porcions de l'antic**: el vidre, l'emmagatzematge, Lua i la resta del que s'ha mesurat (docs/30, docs/95).
local FAMILIES = {
  { name = 'the glass', match = function(w) return w:sub(1, 6) == 'glass.' end },
  { name = 'storage', match = function(w) return w:sub(1, 6) == 'flash.' or w:sub(1, 3) == 'sd.' end },
  { name = 'lua', match = function(w) return w:sub(1, 4) == 'lua.' end },
  { name = 'the rest', match = function(w) return true end },
}

local function meter_table()
  local out = {}
  local words = sys.stats()
  if type(words) ~= 'table' then return out end
  for _, word in ipairs(words) do
    local calls, us, shortest, longest, last_ms = sys.stats(word)
    if type(calls) == 'number' then
      out[#out + 1] = { word = word, calls = calls, us = us, shortest = shortest, longest = longest, last_ms = last_ms }
    end
  end
  return out
end

-- **Les estadístiques ORDENADES pel que costen** (l'amo: *«les stats ordenades»*): the old summary sorted its categories by count, i aquí
-- el que una persona vol saber primer és què s'emporta el temps, o sigui el total de cada comptador de més a menys. Les que no s'han
-- cridat mai van al final, que és on no fan nosa.
local function cost_rows()
  local meters = meter_table()
  table.sort(meters, function(a, b)
    if (a.calls == 0) ~= (b.calls == 0) then return b.calls == 0 end
    if a.us ~= b.us then return a.us > b.us end
    return a.word < b.word
  end)
  local out = {}
  for _, m in ipairs(meters) do
    if m.calls == 0 then
      out[#out + 1] = row(m.word, '-', 'never called')
    else
      local subtitle = string.format('%d call(s) · mean %s', m.calls, human(m.us / m.calls))
      if m.longest ~= nil and m.longest > 0 then
        subtitle = subtitle .. string.format(' · max %s', human(m.longest))
      end
      out[#out + 1] = row(m.word, human(m.us), subtitle)
    end
  end
  return out
end

-- **Els números del pastís**: the control paints `color`, `color+4`, `color+8` and `color+12`, o sigui que la quadrícula de la llegenda
-- es pinta amb el número que li toca i la porció i la seva línia són el mateix to (docs/30).
local function time_slices()
  local meters = meter_table()
  local sum = { 0, 0, 0, 0 }
  for _, m in ipairs(meters) do
    if not kNotAFamily[m.word] then
      for i, family in ipairs(FAMILIES) do
        if family.match(m.word) then
          sum[i] = sum[i] + m.us
          break
        end
      end
    end
  end
  local total = sum[1] + sum[2] + sum[3] + sum[4]
  return sum, total
end

local function memory_rows()
  local out = {}
  local ok, heap = pcall(sys.heap)
  if not ok or type(heap) ~= 'table' then
    out[#out + 1] = row('memory', '-', 'this build cannot say')
    return out
  end
  out[#out + 1] = row('free heap', kb(heap.ram_free), 'the RAM of the board')
  out[#out + 1] = row('largest block', kb(heap.ram_largest), 'in one piece', percent_of(heap.ram_largest, heap.ram_free))
  if heap.extra_total ~= nil and heap.extra_total > 0 then
    out[#out + 1] = row('extra memory', kb(heap.extra_free), string.format('of %s usable', kb(heap.extra_total)),
                        percent_of(heap.extra_free, heap.extra_total))
  else
    out[#out + 1] = row('extra memory', 'none', 'this board has no PSRAM')
  end
  out[#out + 1] = row('lua peak', kb(heap.peak), string.format('of %s allowed', kb(heap.limit)),
                      percent_of(heap.peak, heap.limit))
  out[#out + 1] = row('lua pool', kb(heap.used), string.format('of %s', kb(heap.limit)),
                      percent_of(heap.used, heap.limit))
  return out
end

local function info_rows()
  local out = {}
  local ok, heap = pcall(sys.heap)
  -- **El nom de la placa va a sota i no a la columna del número**: és l'únic valor d'aquesta pàgina que no cap a la dreta a tres
  -- passos (`lilygo-t5s3-epaper-pro`), i tallat amb punts no diu quina placa és (docs/45, docs/92).
  out[#out + 1] = row('Board', sys.emulator and 'emulator' or 'the board', tostring(sys.board or '-'))
  out[#out + 1] = row('Version', tostring(sys.version or '-'), 'lunokOS')
  out[#out + 1] = row('Uptime', human((sys.time_ms() or 0) * 1000), 'since it came up')
  if ok and type(heap) == 'table' then
    out[#out + 1] = row('Heap', kb(heap.ram_free), string.format('largest %s', kb(heap.ram_largest)),
                        percent_of(heap.ram_largest, heap.ram_free))
    if heap.extra_total ~= nil and heap.extra_total > 0 then
      out[#out + 1] = row('PSRAM', kb(heap.extra_free), string.format('of %s usable', kb(heap.extra_total)),
                          percent_of(heap.extra_free, heap.extra_total))
    end
  end
  local okb, b = pcall(power.battery)
  if okb and type(b) == 'table' and b.mv ~= nil then
    out[#out + 1] = row('Battery', string.format('%d mV', b.mv),
                        string.format('%d%%%s', b.pct or 0, b.charging and ' · charging' or ''), b.pct)
  else
    out[#out + 1] = row('Battery', 'unknown', 'this board cannot read its cell')
  end
  if caps.net_wifi then
    local okn, n = pcall(net.status)
    out[#out + 1] = row('WiFi', (okn and type(n) == 'table' and n.connected) and 'yes' or 'no',
                        'the radio of this build')
  end
  return out
end


-- ------------------------------------------------------------------ la pàgina

-- **Els controls de la vista que hi ha al vidre i prou**: canviar de pestanya és tornar-los i fer-ne de nous, perquè aquest
-- sistema no té cap control amagat (docs/30: *«No hi ha cap `visible` i cap control amagat»*). El títol, el peu i la tira de
-- pestanyes no es toquen mai, i la resta se'n va amb `release()` quan la pestanya canvia (docs/30, docs/90).
local title, subtitle, tabs
local bars, pie, legend, strip, the_list
local load_caption, load_chart, mem_caption, mem_chart
local wid, hei = 0, 0

local owned = {}
-- **Els controls de cada fila de Memory i d'Info** (0.192.67): es fan amb la pestanya i es tornen amb ella, o sigui que viuen amb
-- la resta del que és de la vista.
local facts = {}
local cost_data = {}
local total_measured = 0
local read_at = 0

local function keep(control)
  if control ~= nil then owned[#owned + 1] = control end
  return control
end

local function give_back_the_view()
  for i = #owned, 1, -1 do
    local one = owned[i]
    if one ~= nil then pcall(function() one:release() end) end
  end
  owned = {}
  facts = {}
  bars, pie, legend, strip, the_list = nil, nil, nil, nil, nil
  load_caption, load_chart, mem_caption, mem_chart = nil, nil, nil, nil
end

-- ------------------------------------------------------------------ la càrrega i la memòria, mostrejades cada deu segons

-- **La finestra és de l'app i no del control** (docs/30): `ui.chart` dibuixa les mostres que li donen, i qui decideix quantes i
-- cada quant és aquesta pàgina. **Una mostra cada deu segons i mitja hora de finestra** (0.192.60, l'amo: *«Que les gráfiques de
-- la pestanya Load de l'app Stats pugui cabre historial de 30 minuts enrrere»*): cent vuitanta-un punts, que és el que el
-- control ja sap dibuixar des de la 0.192.60 (`kMaxChartSamples` ja no és quaranta-vuit) i el que cab en un bloc de quatre
-- kilobytes. Deu segons i no cinc perquè la finestra és sis vegades més llarga i el bloc no és infinit.
local kSampleMs = 10000
local kSamples = 181
local kWindowMs = 30 * 60 * 1000
-- **I quan la finestra s'omple, la corba camina** (0.192.50, l'amo: *«si s'omple la gráfica es va desplaçant cap a la dreta
-- (scroll)»*): aquí no fa falta cap `span` que no es mogui perquè la finestra ja la talla la història —el que en surt s'oblida—,
-- i el que queda és sempre l'última mitja hora de debò (docs/30, docs/90).
local kGapSeconds = 4 * kSampleMs // 1000
-- **I la memòria de la finestra viu en una sessió pròpia** (0.192.60, l'amo: *«Usa session state per guardar l'historial entre
-- sortir i tornar a l'app»*). No és `state` —allò és la forma de la vista— sinó una caché amb la seva clau, que és el que aquesta
-- casa demana per a una caché (docs/30, docs/75): es torna a trobar en tornar a l'app, i el nucli la desa a la targeta quan l'app
-- s'acaba. Quatre kilobytes per cent vuitanta-una línies de `ms pct10 memkb` (uns disset bytes cada una) i la capçalera.
-- **I la finestra creua els reinicis** (0.192.68, l'amo: *«Load bé, però perd el log entre reinicis?»* —sí que el perdia—): the
-- samples are stamped with the **epoch** (`time.now_ms`) when the board's clock knows it, and with the uptime of this boot
-- (`sys.time_ms`) when it does not —a board without an hour has no way to say when a sample was taken—. The block says which of
-- the two it is in its first line: `v2` is the world's time and `v1` is this boot's, and a block of the other shape is thrown
-- away, perquè una hora d'aran i una del món a la mateixa gràfica serien dues escales i una corba que menteix (docs/30, docs/60,
-- docs/75).
local kHistoryKey = 'load'
local kHistoryLayout = 'v2'
local kHistoryLayoutUptime = 'v1'
local kHistoryBytes = 4096

-- **El rellotge del món, quan n'hi ha**: `time.now_ms()` torna zero mentre ningú no ha posat l'hora (docs/30), i un zero no és
-- una hora: val més l'uptime d'aquesta encesa, que almenys diu quan va ser respecte de l'arrencada.
local function the_world_ms()
  local ok, ms = pcall(time.now_ms)
  if not ok or type(ms) ~= 'number' or ms <= 0 then return nil end
  return ms
end

local function the_layout()
  return (the_world_ms() ~= nil) and kHistoryLayout or kHistoryLayoutUptime
end

-- **El segell d'una mostra i la forma del bloc que li toca**, dits alhora.
local function the_stamp()
  local world = the_world_ms()
  if world ~= nil then return world, kHistoryLayout end
  return sys.time_ms() or 0, kHistoryLayoutUptime
end
-- **I una crida del sistema cada minut** (0.192.60, l'amo: *«M'agradaría que l'app Stats rebés una crida cada minut, però només si
-- la placa està desperta»*): mentre la pestanya Load és al vidre aquesta app es mostreja sola cada deu segons, o sigui que el que
-- afegeix el tick és la mostra de les altres quatre pestanyes —mirar la memòria o el pastís no ha de deixar un forat a la corba— i
-- la de la pàgina que s'ha quedat oberta sense ningú mirant-la (docs/30, docs/60).
local kTickSeconds = 300

-- **Les dues sèries, i la seva hora**: `times` són els segons des de la primera mostra de la finestra, o sigui que el gràfic
-- les col·loca per temps de debò i no pel seu índex (docs/30, docs/90).
local load_values, load_times = {}, {}
local mem_values, mem_times = {}, {}
local load_word, load_pct10 = '-', 0
local mem_used_kb, mem_limit_kb = 0, 0
local sampling_from = nil
local last_sample_ms = nil
-- **El bloc i on s'ha arribat a escriure**: `history_used` és el byte lliure del bloc, o sigui que una mostra nova s'hi afegeix
-- amb disset bytes i no torna a escriure els tres mil que ja hi eren (docs/75).
local history_block = nil
local history_used = 0
local history_layout = nil

local function copy_of(list)
  local out = {}
  for i = 1, #list do out[i] = list[i] end
  return out
end

-- **El bloc de la història, i el guardià de sempre** (0.192.55, docs/30): the session blocks are the first thing the allocator
-- gives back when the memory is short, and every call on one that is gone raises and ends the app. It is asked again here, next
-- to the ask and not on each call.
local function the_history()
  if history_block ~= nil and not history_block:alive() then history_block = nil end
  if history_block == nil then
    local block = sys.session(kHistoryKey, kHistoryBytes)
    if block == nil then return nil end
    history_block = block
  end
  return history_block
end

-- **El text del bloc** (docs/75): the block is zeroed bytes after its tail, o sigui que la cadena s'ha de tallar al primer zero
-- o l'última línia arriba amb fotges i no es pot llegir (el parany que la 0.192.56 va tancar al Paint).
local function the_history_text()
  local block = the_history()
  if block == nil then return nil end
  local text = block:read(0, kHistoryBytes) or ''
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  return text
end

-- **Una llista de línies i prou**: the block is `v1` and then one line per sample —`ms pct mem`, with a `-` where a measurement
-- is missing— so the file of a session can be read by a person, and the app writes it once and not once per field (docs/45).
local function write_the_history(lines)
  local block = the_history()
  if block == nil then return false end
  local text = the_layout() .. '\n'
  if #lines > 0 then text = text .. table.concat(lines, '\n') .. '\n' end
  block:clear()
  block:write(0, text)
  history_used = #text
  history_layout = the_layout()
  return true
end

-- **La finestra que queda, tornada a escriure**: it is what makes the curve walk —the oldest line leaves the block when the window
-- is full— and it only happens when the block has physically filled up, not at every sample (docs/30, docs/75).
local function compact_the_history(keep)
  local text = the_history_text()
  if text == nil then return false end
  local lines = {}
  for line in (text .. '\n'):gmatch('([^\n]*)\n') do
    if line ~= '' and line ~= kHistoryLayout and line ~= kHistoryLayoutUptime then lines[#lines + 1] = line end
  end
  local first = #lines - keep + 1
  if first < 1 then first = 1 end
  local kept = {}
  for i = first, #lines do kept[#kept + 1] = lines[i] end
  return write_the_history(kept)
end

local function push_sample(values, times, value, second)
  values[#values + 1] = value
  times[#times + 1] = second
  while #values > kSamples do table.remove(values, 1) end
  while #times > kSamples do table.remove(times, 1) end
end

-- **Una mostra al bloc, i només disset bytes** (0.192.60): what is written is the new line, not the three kilobytes that were
-- already there —the block is in the memory and the disk only sees it when the app ends—, i quan no hi cap es torna a escriure
-- amb la finestra que queda.
local function remember_the_sample(ms, pct10, mem_kb)
  local block = the_history()
  if block == nil then return false end
  -- **I si el rellotge del món ha arribat ara** (una sincronització amb el GPS amb la pàgina oberta): el bloc canvia de forma i
  -- el que hi havia eren segons d'aquesta encesa, o sigui que es comença de nou en lloc de mesclar dues escales (docs/30, docs/60).
  if history_used == 0 or history_layout ~= the_layout() then
    local head = the_layout() .. '\n'
    block:clear()
    block:write(0, head)
    history_used = #head
    history_layout = the_layout()
  end
  local line = string.format('%d %s %s\n', math.floor(ms),
                             (pct10 ~= nil) and string.format('%d', pct10) or '-',
                             (mem_kb ~= nil) and string.format('%d', mem_kb) or '-')
  if history_used + #line > kHistoryBytes then
    compact_the_history(kSamples)
  end
  if history_used + #line > kHistoryBytes then return false end
  block:write(history_used, line)
  history_used = history_used + #line
  return true
end

-- **La història que hi havia, en entrar a l'app** (0.192.60, l'amo: *«Usa session state per guardar l'historial entre sortir i
-- tornar a l'app»*): the block is read, what is older than the window is forgotten, i el que queda es torna a escriure compactat.
-- Les dues sèries es reconstrueixen d'aquí i no d'una còpia a part (docs/30, docs/75). **I des de la 0.192.68 la finestra creua un
-- reinici**: amb la forma `v2` les mostres són l'època i mitja hora enrere vol dir mitja hora del món, no d'aquesta encesa.
local function load_the_history()
  local text = the_history_text()
  if text == nil then return false end
  -- **Una forma que no és la meva es llença** (docs/30, docs/45): the header is the layout, i un bloc d'una altra versió no es
  -- migra mai —el que hi hagués seria una corba de números que volen dir una altra cosa—. Un `v1` (l'uptime d'una encesa d'abans)
  -- i un `v2` (l'època) no es poden llegir junts, o sigui que el que no és la forma d'ara comença de zero.
  local layout = the_layout()
  if (text:match('^([^\n]*)') or '') ~= layout then
    history_used = 0
    write_the_history({})
    return false
  end
  local now = (layout == kHistoryLayout) and (the_world_ms() or 0) or (sys.time_ms() or 0)
  local kept, lines = {}, 0
  for line in (text .. '\n'):gmatch('([^\n]*)\n') do
    if line ~= '' and line ~= kHistoryLayout and line ~= kHistoryLayoutUptime then
      lines = lines + 1
      local ms, pct, mem = line:match('^(%d+) (%S+) (%S+)$')
      local when = tonumber(ms)
      if when ~= nil and when <= now + 60000 and now - when <= kWindowMs then
        kept[#kept + 1] = line
      end
    end
  end
  while #kept > kSamples do table.remove(kept, 1) end
  load_values, load_times, mem_values, mem_times = {}, {}, {}, {}
  local base = nil
  for i = 1, #kept do
    local ms, pct, mem = kept[i]:match('^(%d+) (%S+) (%S+)$')
    local when = tonumber(ms)
    if base == nil then base = when end
    local second = math.max(0, math.floor((when - base) / 1000))
    local p = tonumber(pct)
    local m = tonumber(mem)
    if p ~= nil then
      push_sample(load_values, load_times, p, second)
      load_pct10 = p
    end
    if m ~= nil then
      push_sample(mem_values, mem_times, m, second)
      mem_used_kb = m
    end
  end
  sampling_from = base
  last_sample_ms = nil
  if lines ~= #kept then return write_the_history(kept) end
  history_used = #text
  history_layout = layout
  return #kept > 0
end

-- **Les dues preguntes d'una mostra.** La càrrega és `power.load()` —el tros de la finestra que el nucli ha passat treballant i no
-- esperant, en dècimes de punt; la finestra és la que hi ha des de l'última mostra, o sigui deu segons quan la pestanya és al
-- vidre— i la memòria és `sys.heap()`: el que aquest sistema té donat a Lua, en KB. Les dues poden faltar (una placa que no
-- mesura la càrrega, una memòria curta) i llavors no s'apunta res: una barra de zero no és una mesura (docs/30).
--
-- **I el forat s'hi veu, no es llença** (0.192.60): abans, una mostra que arribava deu minuts després de l'última esborrava la
-- finestra sencera i la corba començava de zero —una app que s'obre i es tanca cada deu minuts no tenia mai més d'un punt—. Ara
-- la mostra s'apunta on toca i el que hi ha entremig és el que la gràfica dibuixa com a forat (`gap`), que és el que va passar:
-- la placa no ho va mesurar (docs/30, docs/90).
local function take_a_sample()
  -- **El rellotge de les mostres és el del món i el del frec, el de l'encesa** (0.192.68): `now` és el monòton —el que mesura de
  -- quant van dues mostres— i `stamp` és el que s'escriu al bloc, que ha de volar un reinici (docs/30, docs/60).
  local now = sys.time_ms() or 0
  -- **Una mostra que arriba abans d'hora no s'apunta**: the loop of this page (each ten seconds while the tab is on the glass)
  -- and the tick of the system (each minute) can call here in the same turn, i dues mostres al mateix segon serien un punt doble
  -- a la corba i una línia més al bloc per res (docs/30).
  if last_sample_ms ~= nil and now - last_sample_ms < kSampleMs // 2 then return false end
  local stamp, layout = the_stamp()
  -- **Una finestra que ha canviat de forma o que ha saltat comença de nou**: el rellotge del món pot arribar amb una
  -- sincronització del GPS, i aleshores el que hi havia eren segons d'aquesta encesa; un salt més gran que la finestra és una
  -- escala que no vol dir res. Les dues coses es diuen al registre i no es mesclen (docs/30, docs/60).
  if sampling_from == nil or layout ~= history_layout or stamp < sampling_from
     or (stamp - sampling_from) > kWindowMs then
    if sampling_from ~= nil and (layout ~= history_layout or (stamp - sampling_from) > kWindowMs) then
      load_values, load_times, mem_values, mem_times = {}, {}, {}, {}
      write_the_history({})
    end
    sampling_from = stamp
  end
  local second = math.max(0, math.floor((stamp - sampling_from) / 1000))
  local pct10, mem_kb = nil, nil
  local ok, pct, word = pcall(power.load)
  if ok and type(pct) == 'number' then
    pct10 = math.floor(pct)
    load_pct10 = pct10
    load_word = (type(word) == 'string') and word or '-'
    push_sample(load_values, load_times, pct10, second)
  end
  local okm, heap = pcall(sys.heap)
  if okm and type(heap) == 'table' and type(heap.used) == 'number' then
    mem_kb = math.floor(heap.used / 1024)
    mem_used_kb = mem_kb
    mem_limit_kb = math.floor((heap.limit or 0) / 1024)
    if mem_limit_kb < mem_used_kb then mem_limit_kb = mem_used_kb end
    push_sample(mem_values, mem_times, mem_kb, second)
  end
  if pct10 == nil and mem_kb == nil then return false end
  last_sample_ms = now
  remember_the_sample(stamp, pct10, mem_kb)
  return true
end

-- **El text de l'eix és de l'app** (docs/30): la finestra és meva i sé quant ha durat, o sigui que el cap de l'esquerra diu els
-- segons o els minuts que s'està veient i el de la dreta diu que l'última mostra és ara mateix.
-- **El rellotge el demana l'app i el sistema li dona** (0.192.60): `tick.every` posa una entrada a la cua del sistema que **no arma
-- el despertar de la placa** —una app no ha de fer sonar el xip del rellotge per una mostra—, i per defecte **no obre finestra**: si
-- la pàgina no és al davant, la crida es perd i prou (l'amo: *«Però només si l'app demana primer plà. Per defecte no obre
-- finestra.»*). Es demana en obrir i es treu en tancar, que és el que deixa la cua sense el rellotge d'una app que ja no hi és
-- (docs/30, docs/60).
local tick_id = nil

local function arm_the_tick()
  local ok, id = pcall(tick.every, kTickSeconds, 'sample')
  if ok and type(id) == 'number' then tick_id = id end
end

local function axis_left_of(times)
  -- **El cap de l'esquerra és el que es veu i no una constant** (0.192.50, 0.192.60): with the window cut by the history, what is
  -- on the left is the oldest sample the block still has, o sigui que el text diu la veritat de cada moment —`-12 s` quan la
  -- pàgina s'acaba d'obrir, `-30 min` quan la finestra ja és plena— i no una promesa (docs/30).
  local span = (#times > 0) and (times[#times] - times[1]) or 0
  if span >= 120 then return string.format('-%d min', span // 60) end
  return string.format('-%d s', span)
end

local function tenths(pct10)
  return string.format('%d.%d %%', pct10 // 10, pct10 % 10)
end

-- **Les dues gràfiques, amb el número d'ara a sobre de cada una**: la banda del gràfic diu l'escala de la finestra i el que
-- s'ha mesurat fa un moment és a la llegenda, perquè una línia que puja no diu si puja de 3 o de 60 (docs/30, docs/45).
local function show_the_samples()
  if load_chart ~= nil then
    load_chart.values = copy_of(load_values)
    load_chart.times = copy_of(load_times)
    load_chart.min = 0
    load_chart.max = 1000
    load_chart.span = 0
    -- **El forat, en les unitats de l'eix** (0.192.50): `gap` compta el mateix que `times`, que aquí són segons; quatre
    -- mostres perdudes són un forat i la línia s'hi talla (docs/30).
    load_chart.gap = kGapSeconds
    load_chart.left = axis_left_of(load_times)
    load_chart.right = (#load_times > 0) and 'now' or ''
  end
  if load_caption ~= nil then
    load_caption.text = string.format('the load: %s, %s', load_word, tenths(load_pct10))
  end
  if mem_chart ~= nil then
    mem_chart.values = copy_of(mem_values)
    mem_chart.times = copy_of(mem_times)
    mem_chart.min = 0
    mem_chart.max = (mem_limit_kb > 0) and mem_limit_kb or 1024
    mem_chart.span = 0
    mem_chart.gap = kGapSeconds
    mem_chart.left = axis_left_of(mem_times)
    mem_chart.right = (#mem_times > 0) and 'now' or ''
  end
  if mem_caption ~= nil then
    mem_caption.text = string.format('in use: %d KB of %d', mem_used_kb, mem_limit_kb)
  end
end

-- ------------------------------------------------------------------ el dibuix de les barres

local kBarPitch = 42     -- el pas d'una fila: el nom, la barra i el número
local kBarThick = 16     -- el gruix de la barra, el de l'antic
local kBarScale = 3      -- el pas del text de la gràfica
local kNumberW = 150     -- la columna del número, a la dreta (un `3 min 07 s` a tres passos hi cap)

local function fit(text, width, scale)
  if ui.text_width(text, scale) <= width then return text end
  local cut = text
  while #cut > 1 and ui.text_width(cut .. '...', scale) > width do cut = cut:sub(1, #cut - 1) end
  return cut .. '...'
end

-- **Les barres fosques de l'antic** (l'autor de PaperOS: *«les barres de progres de l'app LOG son massa clares, fes-les mes
-- fosques»*, `OLD/PaperOS/src/apps/log_app.cpp`): la resposta a «què s'emporta el temps» s'ha de veure d'un cop d'ull, i amb
-- el gris clar del tema la que més hi surt i la que menys es veuen igual. Cada fila és un comptador —el nom, una barra contra
-- el que més costa i el total a la dreta— i **els comptadors van ordenats pel que costen** (l'amo: *«les stats ordenades»*).
--
-- **I aquesta pàgina no es repinta sola** (docs/45, docs/85): el panell és de tinta i cada passada deixa el fantasma de la que
-- hi havia —l'emulador ho diu igual, `emu_ink.cpp`—, o sigui que el que es llegeix és el que hi havia quan la pestanya es va
-- obrir, i per tenir-ne de nou s'hi torna. L'original feia el mateix amb el seu botó `Refresh`.
local function paint_the_bars(canvas)
  local box = canvas:caps()
  local lh = ui.line_height(kBarScale)
  local gap = 8
  local name_w = math.floor(box.width * 42 / 100)
  local bar_w = box.width - name_w - kNumberW - 2 * gap
  if bar_w < 40 then bar_w = 40 end

  canvas:clear(15)
  local caption = string.format('%d meters, %s measured', #cost_data, human(total_measured))
  canvas:text(0, 0, caption, kBarScale, 4)

  local peak = 0
  for _, m in ipairs(cost_data) do
    if m.us > peak then peak = m.us end
  end

  local top = lh + 10
  local rows = math.floor((box.height - top - lh) / kBarPitch)
  local drawn = 0
  for i = 1, rows do
    local m = cost_data[i]
    if m == nil then break end
    local y = top + (i - 1) * kBarPitch
    local bar_y = y + (kBarPitch - kBarThick) // 2
    local ty = y + (kBarPitch - lh) // 2
    canvas:text(0, ty, fit(m.word, name_w, kBarScale), kBarScale, 0)
    canvas:rect(name_w + gap, bar_y, bar_w, kBarThick, 8)
    if peak > 0 and m.us > 0 then
      local share = math.floor(bar_w * m.us / peak)
      if share < 3 then share = 3 end
      canvas:rect(name_w + gap, bar_y, share, kBarThick, 0)
    end
    local total = human(m.us)
    canvas:text(box.width - ui.text_width(total, kBarScale), ty, total, kBarScale, 0)
    drawn = drawn + 1
  end

  if drawn < #cost_data then
    local rest = string.format('... and %d more: /stats', #cost_data - drawn)
    canvas:text(0, box.height - lh, rest, kBarScale, 4)
  end
end

-- **Els comptadors, llegits una vegada i ordenats pel que costen**: el que no s'ha cridat mai no és una barra buida, és una
-- fila que no hi és (el `/stats` del prompt les diu totes, amb els zeros).
local function count_the_cost()
  local meters = meter_table()
  local out = {}
  local total = 0
  for _, m in ipairs(meters) do
    if m.calls > 0 then
      out[#out + 1] = m
      total = total + m.us
    end
  end
  table.sort(out, function(a, b)
    if a.us ~= b.us then return a.us > b.us end
    return a.word < b.word
  end)
  cost_data = out
  total_measured = total
  read_at = sys.time_ms() or 0
end

-- **El pastís estirat en una tira** (0.192.36): the four tones in the order of the legend, one next to the other, which is the
-- same shape the original's `timeChart_` drew under its pie — a pie is read slowly, a strip is read at a glance, and the two
-- say the same number or one of them is wrong (docs/30, docs/45).
local kStripBar = 26

local function paint_the_strip(canvas)
  local box = canvas:caps()
  local sum, total = time_slices()
  canvas:clear(15)
  if total > 0 then
    local x = 0
    for i = 1, #FAMILIES do
      local w = (i == #FAMILIES) and (box.width - x) or math.floor(box.width * sum[i] / total)
      if w > 0 then canvas:rect(x, 0, w, kStripBar, math.min(15, (i - 1) * 4)) end
      x = x + w
    end
  end
  canvas:frame(0, 0, box.width, kStripBar, 0)
  canvas:text(0, kStripBar + 8, 'the same shares, the glass first', 3, 4)
end

-- ------------------------------------------------------------------ la disposició

-- **La disposició és la de l'antic**: el títol, la tira de vistes i el dibuix; el pastís a l'esquerra amb la llegenda a la
-- seva dreta quan el vidre és més ample que alt, i el pastís a sobre amb la llegenda a sota quan és el Paper —540 × 960—
-- (docs/30, docs/92).
local function geometry()
  local g = { wide = wid > hei, width = wid - 2 * MARGIN }
  g.top = MARGIN + TITLE_H + SUB_H + GAP + TABS_H + GAP
  g.room = hei - g.top - MARGIN
  g.pie = math.min(PIE_SIDE, math.floor(g.room * 6 / 10))
  -- **La pestanya Load: dues gràfiques i dues lletres** (0.192.48). Cada gràfica s'emporta el mateix tros del que queda, que és
  -- el que fa que la càrrega i la memòria es puguin comparar d'un cop d'ull, i la lletra de sobre és la que diu el número d'ara
  -- perquè una línia que puja no diu si puja de tres o de seixanta (docs/30, docs/45).
  g.caption = SUB_H
  g.chart = math.max(96, (g.room - 2 * g.caption - 2 * GAP) // 2)
  return g
end

local function layout()
  local g = geometry()
  local y = MARGIN
  if title ~= nil then
    title:resize(g.width, TITLE_H)
    title:place(MARGIN, y)
  end
  y = y + TITLE_H
  if subtitle ~= nil then
    subtitle:resize(g.width, SUB_H)
    subtitle:place(MARGIN, y)
  end
  y = y + SUB_H + GAP
  if tabs ~= nil then
    tabs:resize(g.width, TABS_H)
    tabs:place(MARGIN, y)
  end
  if bars ~= nil then bars:place(MARGIN, g.top) end
  if load_chart ~= nil then
    load_caption:resize(g.width, g.caption)
    load_caption:place(MARGIN, g.top)
    load_chart:place(MARGIN, g.top + g.caption)
    local below = g.top + g.caption + g.chart + GAP
    mem_caption:resize(g.width, g.caption)
    mem_caption:place(MARGIN, below)
    mem_chart:place(MARGIN, below + g.caption)
  end
  if the_list ~= nil then
    the_list:resize(g.width, g.room)
    the_list:place(MARGIN, g.top)
  end
  if pie ~= nil then
    -- **El pastís a l'esquerra amb la llegenda a la dreta quan el vidre és ample** i el pastís a sobre amb la llegenda a
    -- sota quan és el Paper (docs/30, docs/92): the same two shapes the original reflowed between.
    local px = g.wide and MARGIN or ((g.width - g.pie) // 2 + MARGIN)
    pie:place(px, g.top)
    local legend_y = g.top
    if legend ~= nil then
      local legend_x = g.wide and (px + g.pie + 2 * MARGIN) or MARGIN
      legend_y = g.wide and g.top or (g.top + g.pie + GAP)
      legend:place(legend_x, legend_y)
      if strip ~= nil then strip:place(legend_x, legend_y + legend:caps().height + GAP) end
    end
  end
end

-- ------------------------------------------------------------------ les quatre fulles

-- **La llegenda és un dibuix i no quatre controls de text** (docs/30, docs/45): each row is the square of its slice's tone, the
-- family's name, a dark bar with its share and the time and the percentage on the right, que és el que l'antic posava al
-- costat del pastís. Així el nom, la barra i el número estan alineats per construcció i no per tres amplades que han de
-- coincidir.
local kLegendPitch = 58

local function paint_the_legend(canvas)
  local box = canvas:caps()
  local scale = 3
  local lh = ui.line_height(scale)
  local sum, total = time_slices()
  local peak = 0
  for i = 1, #FAMILIES do
    if sum[i] > peak then peak = sum[i] end
  end
  canvas:clear(15)
  local gap = 10
  local name_w = math.floor(box.width * 30 / 100)
  local number_w = 200
  local bar_x = 26 + name_w + gap
  local bar_w = box.width - bar_x - number_w
  if bar_w < 40 then bar_w = 40 end
  for i, family in ipairs(FAMILIES) do
    local top = (i - 1) * kLegendPitch
    local ty = top + (kLegendPitch - lh) // 2
    local bar_y = top + (kLegendPitch - kBarThick) // 2
    -- **El to de la porció**: `color`, `+4`, `+8` i `+12`, que és exactament el que camina el pastís, o sigui que la
    -- quadrícula de la llegenda i el seu tros del cercle no poden discrepar (docs/30, docs/90).
    canvas:rect(0, bar_y, 16, 16, math.min(15, (i - 1) * 4))
    canvas:frame(0, bar_y, 16, 16, 0)
    canvas:text(26, ty, family.name, scale, 0)
    canvas:rect(bar_x, bar_y, bar_w, kBarThick, 8)
    if peak > 0 and sum[i] > 0 then
      local share = math.floor(bar_w * sum[i] / peak)
      if share < 3 then share = 3 end
      canvas:rect(bar_x, bar_y, share, kBarThick, 0)
    end
    local share_of_total = (total > 0) and (sum[i] * 100 / total) or 0
    local words = string.format('%s · %d%%', human(sum[i]), math.floor(share_of_total + 0.5))
    canvas:text(box.width - ui.text_width(words, scale), ty, words, scale, 0)
  end
  -- **El total i el que no hi és**: `kernel.loop` és el torn sencer i `queue.wait` i `lua.own` en són trossos, o sigui que
  -- sumar-los tots diria el temps dues vegades; el peu ho diu, perquè una roda que no quadra amb la llista és una roda que
  -- menteix (docs/30, docs/95).
  local foot = string.format('%s, without the turn itself', human(total))
  canvas:text(0, box.height - lh, foot, scale, 4)
end

-- **Una fila de Memory o d'Info és un control de l'app, i per això hi cap una barra** (0.192.67): `ui.list` amb `items` dibuixa
-- el títol, el número i la lletra de sota, i res més; amb **entrades** (`ui.list_entry`) la fila és meva i hi poso el que hi ha
-- d'anar: dues lletres a tres passos i la **barra del sistema** (`ui.progress`) quan la fila té un total amb què comparar-se.
-- `facts` guarda els controls de cada fila per tornar-los a omplir sense tornar-los a fer (docs/30, docs/90).
local kFactBar = 16

-- **L'alçada d'una fila es compta amb la lletra que hi ha** (0.192.67): tres bandes —el títol i el número, la lletra de sota i la
-- barra— i el pas de la lletra el diu el sistema, no aquesta pàgina. Donar-lo per fet va deixar la barra sota la vora de la fila,
-- on el dibuix la talla (docs/30, docs/90).
local function fact_row_height()
  return 4 + 2 * ui.line_height(3) + 4 + kFactBar + 2
end

local function a_label(width, height, text, color, align)
  local label = ui.label(width, height)
  if label == nil then return nil end
  label.text = text
  label.scale = 3
  label.align = align or 'left'
  label.color = color or 0
  label.background = 15
  return label
end

local function fill_a_fact(one, controls)
  if controls.title ~= nil then controls.title.text = one.title end
  if controls.value ~= nil then controls.value.text = one.detail end
  if controls.subtitle ~= nil then controls.subtitle.text = one.subtitle or '' end
  if controls.bar ~= nil and one.bar ~= nil then controls.bar.value = one.bar end
end

-- **Les files d'una pestanya, fetes una vegada**: el nombre de files no canvia mentre la pestanya és al vidre, o sigui que els
-- controls es fan en entrar-hi i el que es refresca és el seu contingut (docs/30, docs/75).
local function build_the_facts(width, row_height)
  facts = {}
  local list_of = (shape.tab == kMemoryTab) and memory_rows() or info_rows()
  local lh = ui.line_height(3)
  for _, one in ipairs(list_of) do
    local entry = ui.list_entry(width, row_height)
    if entry ~= nil then
      entry.background = 15
      entry.separator = true
      local controls = {}
      local value_w = ui.text_width(one.detail, 3) + 10
      if value_w < 150 then value_w = 150 end
      if value_w > math.floor(width * 46 / 100) then value_w = math.floor(width * 46 / 100) end
      controls.title = a_label(width - value_w - 30, lh + 4, one.title, 0, 'left')
      if controls.title ~= nil then
        controls.title:place(12, 4)
        entry:add(controls.title)
      end
      controls.value = a_label(value_w, lh + 4, one.detail, 0, 'right')
      if controls.value ~= nil then
        controls.value:place(width - value_w - 12, 4)
        entry:add(controls.value)
      end
      controls.subtitle = a_label(width - 24, lh + 4, one.subtitle or '', 4, 'left')
      if controls.subtitle ~= nil then
        controls.subtitle:place(12, 4 + lh + 2)
        entry:add(controls.subtitle)
      end
      -- **La barra a sota, ampla i del sistema** (l'amo: *«no podem anidar-la?»*): `track` és la pista i `fill` el tros fet, tots
      -- dos tons del paper d'aquesta casa, i **només hi és a la fila que té un total amb què comparar-se** —una barra buida diria
      -- un zero que no és cert— (docs/30, docs/90).
      if one.bar ~= nil then
        local bar = ui.progress(width - 24, kFactBar)
        if bar ~= nil then
          bar.value = one.bar
          bar.background = 15
          bar.track = 8
          bar.fill = 0
          bar.border = 4
          bar:place(12, 4 + 2 * lh + 4)
          entry:add(bar)
          controls.bar = bar
        end
      end
      facts[#facts + 1] = controls
      the_list:add(entry)
    end
  end
end

-- **I el que canvia és el número, no la fila**: `refresh` torna a preguntar les files i les aboca als controls que ja hi són.
local function refresh_the_facts()
  if the_list == nil or #facts == 0 then return end
  local list_of = (shape.tab == kMemoryTab) and memory_rows() or info_rows()
  for i, one in ipairs(list_of) do
    local controls = facts[i]
    if controls ~= nil then fill_a_fact(one, controls) end
  end
end

local function refresh()
  if the_list ~= nil then
    refresh_the_facts()
    the_list.page = shape.page
    shape.page = the_list.page
    save_state()
  end
  -- **La pestanya Load es mostra sempre que s'obre i després cada deu segons** (0.192.48): la primera mostra es pren aquí, o
  -- sigui que una pestanya que s'acaba d'obrir no ensenya una gràfica buida esperant el rellotge.
  if load_chart ~= nil then
    take_a_sample()
    show_the_samples()
  end
  if pie ~= nil then
    -- **El pastís rep el que es veu i no el que es mesura**: el pont talla cada porció a 65535, i els microsegons d'una
    -- estona de feina passen d'un milió, o sigui que la proporció ha de viatjar petita —dècimes de punt— i dir el mateix
    -- que la llegenda (docs/30, docs/90).
    local sum, total = time_slices()
    local scaled = { 0, 0, 0, 0 }
    for i = 1, #FAMILIES do
      scaled[i] = (total > 0) and math.floor(sum[i] * 1000 / total + 0.5) or 0
    end
    pie.slices = scaled
  end
end

local function set_the_body(wide)
  if title ~= nil then title.scale = wide and 5 or 4 end
  -- **La tira de pestanyes no es toca** (l'amo: *«A tot l'app Stats, excepte les pestanyes»*).
  if tabs ~= nil then tabs.scale = wide and 3 or 2 end
  if the_list ~= nil then
    the_list.row_height = fact_row_height()
    the_list.scale = 3
    -- **El valor de la dreta és el número que es busca**: vuit és un gris que sobre aquest paper no es llegeix, i el que ha de
    -- dir-se es diu amb tinta (docs/30, docs/92).
    the_list.text = 0
    the_list.dim = 4
  end
  if subtitle ~= nil then
    subtitle.scale = 3
    subtitle.align = 'left'
    subtitle.color = 4
  end
end

local function build_the_view()
  local g = geometry()
  if shape.tab == kCostTab then
    count_the_cost()
    bars = keep(ui.canvas(g.width, g.room))
    if bars ~= nil then
      bars.on_draw = function(self) paint_the_bars(self) end
      bars:place(MARGIN, g.top)
    end
    if subtitle ~= nil then
      subtitle.text = string.format('read %s after the boot', human(read_at * 1000))
    end
  elseif shape.tab == kTimeTab then
    pie = keep(ui.pie(g.pie, g.pie))
    if pie ~= nil then
      pie.color = 0
      pie.slices = { 0, 0, 0, 0 }
    end
    legend = keep(ui.canvas(g.width, #FAMILIES * kLegendPitch + ui.line_height(3) + 8))
    if legend ~= nil then
      legend.on_draw = function(self) paint_the_legend(self) end
    end
    strip = keep(ui.canvas(g.width, kStripBar + ui.line_height(3) + 8))
    if strip ~= nil then
      strip.on_draw = function(self) paint_the_strip(self) end
    end
    if subtitle ~= nil then subtitle.text = 'where the time went' end
  elseif shape.tab == kLoadTab then
    -- **Les dues gràfiques de la pestanya nova** (0.192.48): la càrrega a sobre i la memòria a sota, amb el seu número a sobre
    -- de cada una. El `decimals = 1` de la càrrega és el que fa que mitja dècima de punt es vegi (`3.4 %`) en un gràfic d'un
    -- centenar de línies; la memòria va en KB sencers perquè un byte no és una cosa que una persona llegeixi (docs/30, docs/90).
    load_caption = keep(ui.label(g.width, g.caption))
    if load_caption ~= nil then
      load_caption.scale = 3
      load_caption.align = 'left'
      load_caption.color = 0
      load_caption.text = 'the load: -'
    end
    load_chart = keep(ui.chart(g.width, g.chart))
    if load_chart ~= nil then
      load_chart.suffix = '%'
      load_chart.decimals = 1
      load_chart.min = 0
      load_chart.max = 1000
      load_chart.scale = g.wide and 4 or 3
      load_chart.color = 0
    end
    mem_caption = keep(ui.label(g.width, g.caption))
    if mem_caption ~= nil then
      mem_caption.scale = 3
      mem_caption.align = 'left'
      mem_caption.color = 0
      mem_caption.text = 'in use: -'
    end
    mem_chart = keep(ui.chart(g.width, g.chart))
    if mem_chart ~= nil then
      mem_chart.suffix = ' KB'
      mem_chart.decimals = 0
      mem_chart.min = 0
      mem_chart.max = 1024
      mem_chart.scale = g.wide and 4 or 3
      mem_chart.color = 0
    end
    if subtitle ~= nil then subtitle.text = 'every 10 s, half an hour' end
  else
    the_list = keep(ui.list(g.width, g.room))
    if the_list ~= nil then
      the_list.row_height = fact_row_height()
      the_list.on_page = function(self, page)
        shape.page = page
        save_state()
      end
      build_the_facts(g.width, the_list.row_height)
    end
    if subtitle ~= nil then
      subtitle.text = (shape.tab == kMemoryTab) and 'the memory of this board'
                                                 or tostring(sys.board or '-')
    end
  end
  set_the_body(g.wide)
end

local show_tab

local function build()
  local c = ui.caps()
  wid, hei = c.width, c.height
  ui.clear(15)
  title = ui.label(wid - 2 * MARGIN, TITLE_H)
  if title ~= nil then
    title.text = 'Stats'
    title.scale = 4
    title.align = 'left'
  end
  subtitle = ui.label(wid - 2 * MARGIN, SUB_H)
  tabs = ui.tabs(wid - 2 * MARGIN, TABS_H)
  if tabs ~= nil then
    tabs.items = TABS
    tabs.index = shape.tab
    tabs.on_change = function(_, index) show_tab(index) end
  end
end

-- **La pestanya que es tria és una vista nova**: el que hi havia es torna, el nou es fa amb la mida que té el vidre ara i es
-- col·loca, i el cercle es queda a la tira de pestanyes per poder-ne canviar una altra amb el dial (docs/30, docs/90).
show_tab = function(index)
  if index == nil or index < 1 or index > #TABS then return end
  if index ~= shape.tab then
    shape.tab = index
    shape.page = 1
    save_state()
    give_back_the_view()
    ui.clear(15)
    build_the_view()
    layout()
    refresh()
  end
  if tabs ~= nil then
    tabs.index = shape.tab
    ui.focus(tabs)
  end
  ui.present()
end

-- ------------------------------------------------------------------ l'app

-- **Una finestra no és una pàgina** (0.192.70): `console.is_foreground()` és fals quan el sistema ha obert l'app en una finestra
-- per donar-li una crida —el tick— sense posar-la al vidre. En aquest cas el que es fa és el que la crida necessita: carregar la
-- forma i la història, deixar els ganxos definits i prou. Res de construir la pàgina, res de pintar i cap bucle: la finestra dura
-- una crida i es tanca (docs/30, docs/60).
local in_a_window = not console.is_foreground()

load_state()
-- **La finestra que hi havia es torna a trobar abans de pintar-la** (0.192.60): the block of the session is the history, i el que
-- `refresh` pinta és el que ja hi era més la mostra d'ara, no una gràfica buida que s'omple a poc a poc (docs/30, docs/75).
load_the_history()

-- **La pàgina es renta sempre que un control se'n va** (0.192.36): el forat que deixa un canvas alliberat no és el paper de la
-- pàgina —surt un gris de targeta—, i el que el tapa és el `ui.clear` de l'app, que és el que fa el `show_tab` i el que fan
-- aquestes dues portes (docs/30, docs/90).
function on_resume()
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  refresh()
  ui.present()
end

-- **El vidre ha canviat de mida**: un canvas neix amb una mida i no se li'n pot donar una altra, o sigui que la vista es
-- torna a fer (docs/30, docs/90).
function on_resize(w, h)
  wid, hei = w, h
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  refresh()
  ui.present()
end

function on_pause() save_state() end
function on_save() save_state() end
function on_close()
  save_state()
  -- **El rellotge no marxa amb la pàgina** (0.192.69, l'amo: *«la crida "tick" desapareix quan l'app es tanca?… si es així de que
  -- serveix?… La idea del tick es que continui durant tota la sessió fins reiniciar o apagar»*): the tick is the app's for the
  -- whole session —asking again with the same configuration gives the same id back, o sigui que la cadència no es reinicia mai
  -- quan la pàgina torna— i el que no fa mentre la pàgina no hi és és apartar la pàgina de la persona per prendre una mostra. El
  -- forat que deixa es veu a la corba, que és el que va passar (docs/30, docs/60).
end

-- **La crida del sistema, cada minut mentre la placa és desperta** (0.192.60): it does what the loop does —one sample—, i amb la
-- pestanya Load al vidre **no cal que faci res**: el bucle ja en pren una cada deu segons, i el tick només existeix per a quan la
-- pàgina mira una altra pestanya (docs/30, docs/85). **I la resposta es diu** (0.192.65): `false` vol dir «no hi havia res a
-- fer amb aquesta», i el registre ho escriu (`#4 for stats: the app had nothing to add`), que és l'única manera de saber des de
-- fora què ha fet l'app amb la crida.
function on_tick(payload, meta)
  if load_chart ~= nil then return false end
  return take_a_sample()
end

-- **I la finestra acaba aquí**: els ganxos ja hi són, el sistema crida `on_tick` just després del chunk i el que la crida ha
-- escrit al bloc baixa al disc quan la finestra es tanca (docs/30, docs/60, docs/75).
if in_a_window then return end

build()
build_the_view()
layout()
refresh()
arm_the_tick()
ui.focus(tabs)
ui.present()

-- **El que aquest bucle ha de fer és deixar girar el nucli** (docs/30): un `present` sense res de nou no pinta ni un píxel, i
-- aquesta pàgina no canvia sola —el que ha canviat es llegeix tornant a la pestanya—. **L'excepció és la pestanya Load**
-- (0.192.48, l'amo: *«refresc de les gráfiques temporitzat mentre la vista estigui activa, cada 10 segons»*): mentre és al vidre
-- es pregunta la càrrega i la memòria cada deu segons i es pinta el que ha canviat, amb `gl16`, que és la passada que no fa
-- parpellejar el panell (docs/30, docs/50, docs/85).
while true do
  ui.present()
  if load_chart ~= nil then
    local now = sys.time_ms() or 0
    if last_sample_ms == nil or now - last_sample_ms >= kSampleMs then
      take_a_sample()
      show_the_samples()
      ui.present('gl16')
    end
  end
  sys.delay(50):await()
end
