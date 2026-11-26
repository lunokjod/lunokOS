-- gps: **el cel, tal com el receptor el veu** (0.192.63, l'amo: *«Fes un app de GPS … Mostrar tota l'info que aquest MIA-M10Q GNSS
-- ens pugui donar. I mostrar-ho a la pantalla, si pot ser visualment millor.»*).
--
-- Dues coses i en aquest ordre, perquè són les dues preguntes d'una persona que encén un receptor:
--
--   · **on és** —la latitud i la longitud, l'altitud, la velocitat i el rumb, amb la qualitat de la posició—, dibuixat gran i amb els
--     decimals que el receptor dona, que són centímetres;
--   · **per què no en té, si no en té** —el **mapa del cel**: un cercle amb els satèl·lits de l'últim GSV col·locats on són (l'azimut
--     és l'angle, l'elevació és la distància al centre) i la força de cada un. Un receptor que es queixa sense dir què ve és un receptor
--     que deixa la persona mirant una pàgina buida; un mapa del cel diu d'un cop si el problema és el cel tapat o l'antena.
--
-- **I l'hora**: la del cel (UTC), la que el sistema creu i si se l'ha cregut —quan i quant s'ha mogut el rellotge—, perquè la
-- sincronització és una cosa que es pot veure i no una promesa d'un commutador (docs/30, docs/60).
--
-- **El que aquesta pàgina no fa**: no engega ni apaga el receptor si el sistema el necessita. L'encén en obrir-se —una pàgina del cel
-- sense cel no ensenya res— i **no l'apaga en tancar-se quan el commutador de Settings diu que el rellotge es creu el GPS**: el mòdul és
-- del sistema i no d'aquesta pàgina (docs/30, docs/50).

local caps = sys.caps()
if not caps.display then
  console.print('gps: this board has no glass')
  return
end

-- **`v1`**: la primera forma d'aquesta pàgina; una forma d'una altra versió es llença (docs/30, docs/45).
local kLayout = 'v1'
local kStateBytes = 256

local MARGIN = 14
local TITLE_H = 46
local SUB_H = 22
local GAP = 10
local NOTE_H = 22

-- **Cada quant es torna a llegir el cel i cada quant es repinta** (0.192.63): el nucli llegeix el receptor a cada volta —no té cap
-- interrupció per fer-ho millor—, o sigui que aquesta pàgina només ha de decidir quan dibuixa. **Un segon per als números i sis per al
-- mapa**: el paper és de tinta i cada passada deixa el fantasma de l'anterior, i un rellotge de segons que es repinta cada segon no diu
-- res que no digués el de fa dos (docs/30, docs/85).
local kReadMs = 1000
local kNumbersMs = 2000
local kSkyMs = 6000

local wid, hei = 540, 892
local title, subtitle, sky_canvas, info_canvas, footer

local state_block = nil

local function the_state()
  -- **Un bloc que el sistema s'ha quedat no es pot fer servir** (0.192.55, docs/30): the session blocks are the first thing the
  -- allocator gives back when the memory is short, and every call on one that is gone raises and ends the app.
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
  block:write(0, string.format('%s\n', kLayout))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return end
  local text = block:read(0, kStateBytes) or ''
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  if (text:match('^([^\n]*)') or '') ~= kLayout then return end
end

-- ------------------------------------------------------------------ el que el receptor diu, dit com ho diu una persona

-- **Els graus amb set decimals i el costat del món**: 10⁻⁷ de grau és un centímetre al terra, o sigui que els decimals del receptor
-- són tots visibles i cap és decoratiu (docs/50).
local function lat_lon(value_e7, positive, negative)
  local value = value_e7 or 0
  local side = (value < 0) and negative or positive
  if value < 0 then value = -value end
  return string.format('%.7f %s', value / 10000000, side)
end

-- **La distància com la llegeix una persona**: metres quan és a prop i quilòmetres quan no (el receptor la dona en centímetres).
local function metres(value)
  local m = value or 0
  if m >= 1000 or m <= -1000 then return string.format('%.2f km', m / 1000) end
  return string.format('%.1f m', m)
end

-- **La velocitat del pont és en metres per segon** i no en centímetres: `gps.info()` dona les distàncies en metres i el que una persona
-- llegeix és km/h, o sigui que el factor és 3,6 i prou (docs/30).
local function kmh(m_s)
  return string.format('%.1f km/h', (m_s or 0) * 3.6)
end

-- **L'hora del cel, que és UTC**: es diu així i no es converteix, perquè una pàgina que ensenyi l'hora del satèl·lit com si fos la de
-- la persona no deixaria veure si el rellotge de la placa va bé (docs/30, docs/60).
local function sky_time(time)
  if time == nil or not time.valid then return 'no date from the sky' end
  return string.format('%04d-%02d-%02d %02d:%02d:%02d UTC', time.year or 0, time.month or 0, time.day or 0, time.hour or 0,
                       time.minute or 0, time.second or 0)
end

-- **La qualitat de la posició, amb el nom que el receptor dona**: 0 és cap, 1 GPS, 2 diferencial, 4 RTK fix, 5 RTK flotant i 6
-- estimada. Un número sol no diu si una posició és de centímetres o de deu metres (docs/50).
local kQuality = {
  [0] = 'no fix',
  [1] = 'GPS',
  [2] = 'DGPS',
  [3] = 'PPS',
  [4] = 'RTK fixed',
  [5] = 'RTK float',
  [6] = 'estimated',
}

-- Els vuit punts del rumb, perquè un número de graus no diu cap a on va ningú.
local kPoints = { 'N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW' }

local function course_name(deg10)
  local point = (math.floor((deg10 or 0) / 10) + 22) // 45
  if point < 0 then point = 0 end
  if point > 7 then point = 7 end
  return kPoints[point + 1]
end

-- ------------------------------------------------------------------ l'estat que es llegeix

local the_fix = nil

local function read_the_sky()
  local ok, info = pcall(gps.info)
  if ok and type(info) == 'table' then
    the_fix = info
  else
    the_fix = nil
  end
  return the_fix
end

local function powered()
  local ok, about = pcall(gps.caps)
  return ok and type(about) == 'table' and about.powered == true
end

-- ------------------------------------------------------------------ el mapa del cel

-- **El cercle del cel**: el centre és el zènit i la vora l'horitzó, o sigui que un satèl·lit a 90° d'elevació cau al mig i un a 0° a
-- la vora —que és exactament com es dibuixa un mapa del cel de tota la vida, i el que fa que es vegi d'un cop si el que falta és cel
-- baix o cel alt—. L'azimut és l'angle: el nord a dalt, l'est a la dreta (docs/30, docs/50).
local kSkyRings = { 30, 60 }

local function paint_the_sky(canvas)
  local box = canvas:caps()
  canvas:clear(15)
  local scale = 3
  local lh = ui.line_height(scale)
  local radius = math.min(box.width, box.height) // 2 - lh - 8
  if radius < 40 then radius = 40 end
  local cx = box.width // 2
  local cy = box.height // 2

  -- Les anelles de 30 i 60 graus d'elevació, i la creu dels punts cardinals: el marc del mapa.
  local ring_level = 11
  canvas:circle(cx, cy, radius, ring_level)
  for i = 1, #kSkyRings do
    local r = radius * (90 - kSkyRings[i]) // 90
    if r > 6 then canvas:circle(cx, cy, r, ring_level) end
  end
  canvas:line(cx - radius, cy, cx + radius, cy, ring_level)
  canvas:line(cx, cy - radius, cx, cy + radius, ring_level)
  canvas:text(cx - ui.text_width('N', scale) // 2, cy - radius - lh - 2, 'N', scale, 8)
  canvas:text(cx + radius + 4, cy - lh // 2, 'E', scale, 8)
  canvas:text(cx - ui.text_width('S', scale) // 2, cy + radius + 2, 'S', scale, 8)
  canvas:text(cx - radius - ui.text_width('W', scale) - 4, cy - lh // 2, 'W', scale, 8)

  -- **I els satèl·lits**: el punt és el número del satèl·lit, la mida diu si el receptor el compta per a la posició i el to la
  -- força del senyal —el negre és el que arriba fort i el gris el que amb prou feines es veu— (docs/30).
  local sky = {}
  local ok, list = pcall(gps.sky)
  if ok and type(list) == 'table' then sky = list end
  for i = 1, #sky do
    local one = sky[i]
    local elevation = one.elevation or 0
    local azimuth = one.azimuth or 0
    if elevation > 90 then elevation = 90 end
    local r = radius * (90 - elevation) // 90
    local angle = math.rad((azimuth or 0) - 90)
    local x = cx + math.floor(math.cos(angle) * r)
    local y = cy + math.floor(math.sin(angle) * r)
    local snr = one.snr or 0
    local level = 0
    if snr > 0 and snr < 20 then
      level = 6
    elseif snr >= 20 and snr < 35 then
      level = 3
    end
    local size = one.used and 4 or 3
    canvas:disc(x, y, size, level)
    if one.used then canvas:circle(x, y, size + 1, 0) end
    -- **El número del satèl·lit, a dues escales**: a una no es llegia i és el que una persona compara amb el que diu un altre receptor
    -- (docs/30).
    local words = string.format('%d', one.id or 0)
    canvas:text(x - ui.text_width(words, 2) // 2, y - size - ui.line_height(2) - 1, words, 2, 8)
  end

  if #sky == 0 then
    local words = powered() and 'no satellite in view' or 'the receiver is off'
    canvas:text(cx - ui.text_width(words, scale) // 2, cy - lh // 2, words, scale, 8)
  end
end

-- ------------------------------------------------------------------ els números

local function paint_the_numbers(canvas)
  local box = canvas:caps()
  canvas:clear(15)
  -- **La lletra, tan gran com cap a la pàgina** (0.192.63, l'amo: *«Fes la lletra mes grossa, encara tens vista»*): el canvas dels
  -- números fa dues-centes vint píxels i el que hi ha són set línies, o sigui que l'escala gran de la vista (6) hi cap a la posició i la
  -- del mig (3) a la resta. El que era a quatre i dos s'havia de llegir amb el nas a la tinta (docs/30, docs/90).
  local big = 6
  local small = 3
  local lh_small = ui.line_height(small)
  local lh_big = ui.line_height(big)
  local y = 2
  local fix = the_fix

  if fix == nil then
    canvas:text(0, y, 'the receiver is off', small, 8)
    return
  end

  if not fix.fix then
    -- **El que es pot dir sense posició**: el receptor parla, quants satèl·lits veu i que encara no ha tancat el cercle. És la
    -- diferència entre «espera» i «no hi ha cel», que és el que una persona necessita saber (docs/30, docs/50).
    canvas:text(0, y, 'no position yet', big, 0)
    y = y + lh_big + 6
    local words = string.format('%d satellite(s) in view, %d sentence(s) read', fix.satellites_seen or 0, fix.sentences or 0)
    canvas:text(0, y, words, small, 8)
    y = y + lh_small + 4
    if (fix.sentences or 0) == 0 and powered() then
      canvas:text(0, y, 'the module has not said anything yet: check the wire and the bauds', small, 8)
    end
    return
  end

  -- **La posició, gran**: és el que una persona ha vingut a veure i el que es pot dictar per telèfon (docs/30).
  canvas:text(0, y, lat_lon(fix.lat_e7, 'N', 'S'), big, 0)
  y = y + lh_big + 4
  canvas:text(0, y, lat_lon(fix.lon_e7, 'E', 'W'), big, 0)
  y = y + lh_big + 8

  -- **Una cosa per línia** (0.192.63): a l'escala que es llegeix de lluny, una línia només hi cap si diu tres o quatre coses, i el que
  -- es talla no és un detall sinó l'últim número de la fila. Cada línia és una pregunta —quant, com, amb què, quan— i així cap es perd
  -- (docs/30, docs/90).
  local line = string.format('%s  ·  %s  ·  %s %0.1f°', metres(fix.altitude), kmh(fix.speed), course_name(fix.course),
                             fix.course or 0)
  canvas:text(0, y, line, small, 8)
  y = y + lh_small + 4

  line = string.format('%s  ·  %d of %d satellites', kQuality[fix.quality or 0] or 'quality ?', fix.satellites_used or 0,
                       fix.satellites_seen or 0)
  canvas:text(0, y, line, small, 8)
  y = y + lh_small + 4

  line = string.format('hdop %0.1f  ·  vdop %0.1f  ·  pdop %0.1f', fix.hdop or 0, fix.vdop or 0, fix.pdop or 0)
  canvas:text(0, y, line, small, 8)
  y = y + lh_small + 8

  -- **I l'hora, que és la meitat del que el cel dona**: la del satèl·lit en UTC i el que ha arribat a la placa (docs/30, docs/60).
  canvas:text(0, y, sky_time(fix.time), small, 0)
  y = y + lh_small + 4
  line = string.format('%d sentence(s), %d bad  ·  %s', fix.sentences or 0, fix.bad_sentences or 0,
                       (fix.ms_since_a_fix or 0) > 0 and string.format('%d s ago', (fix.ms_since_a_fix or 0) // 1000) or 'now')
  canvas:text(0, y, line, small, 8)
end

-- ------------------------------------------------------------------ la pàgina

local function the_subtitle()
  local fix = the_fix
  if fix == nil or not powered() then return 'the receiver is off' end
  if fix.fix then
    return string.format('the sky: %d of %d satellites, hdop %0.1f', fix.satellites_used or 0, fix.satellites_seen or 0,
                         fix.hdop or 0)
  end
  if (fix.sentences or 0) == 0 then return 'waiting: the module has not spoken yet' end
  return string.format('waiting for a position: %d satellite(s) in view', fix.satellites_seen or 0)
end

local function the_footer()
  local fix = the_fix
  local said
  if fix ~= nil and fix.synced_ms ~= nil then
    said = string.format('the sky set the clock %d s ago', (fix.synced_ms or 0) // 1000)
  elseif fix ~= nil and fix.fix then
    said = 'the clock has not been set from the sky'
  else
    said = 'the sky does not set the clock while there is no position'
  end
  local ok, sync = pcall(cfg.gps_sync)
  if ok and sync == true then return said .. ' · Settings: on' end
  return said .. ' · Settings: off'
end

-- **Les mides es calculen una vegada i les dues meitats les fan servir** (0.192.63): un canvas amb buffer **no es pot
-- redimensionar** —els seus píxels són un bloc demanat amb una mida i una altra mida és un altre canvas—, o sigui que la mida que es
-- dona a `ui.canvas` ha de ser la definitiva i no la d'un tanteig. Amb les mides a mitges el canvas es quedava amb l'alçada del
-- tanteig i les últimes línies dels números queien fora, que és el que es va veure a la primera captura (docs/30, docs/90).
local function sizes()
  local top = MARGIN + TITLE_H + SUB_H + GAP
  local room = hei - top - MARGIN - NOTE_H - GAP
  local sky_h = math.floor(room * 52 // 100)
  if sky_h < 200 then sky_h = 200 end
  local info_h = room - sky_h
  if info_h < 120 then info_h = 120 end
  return sky_h, info_h
end

local function layout()
  local y = MARGIN
  if title ~= nil then
    title:resize(wid - 2 * MARGIN, TITLE_H)
    title:place(MARGIN, y)
  end
  y = y + TITLE_H
  if subtitle ~= nil then
    subtitle:resize(wid - 2 * MARGIN, SUB_H)
    subtitle:place(MARGIN, y)
  end
  y = y + SUB_H + GAP
  local sky_h = sizes()
  if sky_canvas ~= nil then
    sky_canvas:place(MARGIN, y)
  end
  y = y + sky_h + GAP
  if info_canvas ~= nil then
    info_canvas:place(MARGIN, y)
  end
  if footer ~= nil then
    footer:resize(wid - 2 * MARGIN, NOTE_H)
    footer:place(MARGIN, hei - MARGIN - NOTE_H)
  end
end

local function refresh_the_words()
  if subtitle ~= nil then subtitle.text = the_subtitle() end
  if footer ~= nil then footer.text = the_footer() end
end

local function build()
  local c = ui.caps()
  wid, hei = c.width, c.height
  ui.clear(15)
  title = ui.label(wid - 2 * MARGIN, TITLE_H)
  if title ~= nil then
    title.text = 'GPS'
    title.scale = 5
    title.align = 'left'
  end
  subtitle = ui.label(wid - 2 * MARGIN, SUB_H)
  if subtitle ~= nil then
    subtitle.scale = 2
    subtitle.align = 'left'
    subtitle.color = 8
  end
  local sky_h, info_h = sizes()
  sky_canvas = ui.canvas(wid - 2 * MARGIN, sky_h)
  info_canvas = ui.canvas(wid - 2 * MARGIN, info_h)
  footer = ui.label(wid - 2 * MARGIN, NOTE_H)
  if footer ~= nil then
    footer.scale = 2
    footer.align = 'left'
    footer.color = 8
  end
end

-- ------------------------------------------------------------------ l'app

load_state()
build()
layout()

-- **El receptor s'encén quan la pàgina s'obre** (0.192.63): una pàgina del cel amb el mòdul apagat és una pàgina que no ensenya res, i el
-- que la persona ha demanat és mirar-lo. Si el commutador de Settings ja el té encès, això no fa res (docs/30, docs/50).
if caps.gps then
  local ok, why = pcall(gps.on)
  if not ok and console ~= nil then
    console.print('gps: the receiver does not answer: ' .. tostring(why))
  end
end
read_the_sky()
refresh_the_words()
if sky_canvas ~= nil then paint_the_sky(sky_canvas) end
if info_canvas ~= nil then paint_the_numbers(info_canvas) end
ui.present()
save_state()

local last_read = sys.time_ms() or 0
local last_numbers = last_read
local last_sky = last_read

-- **El bucle llegeix i repinta quan toca** (docs/30): el nucli ja llegeix el receptor a cada volta, o sigui que aquí només es decideix
-- quant es torna a dibuixar. Un rellotge de segons no demana un repintat per segon i una pàgina de tinta encara menys.
while true do
  sys.delay(250):await()
  local now = sys.time_ms() or 0
  if now - last_read >= kReadMs then
    last_read = now
    read_the_sky()
    refresh_the_words()
  end
  if now - last_numbers >= kNumbersMs then
    last_numbers = now
    if info_canvas ~= nil then
      paint_the_numbers(info_canvas)
      info_canvas:invalidate()
    end
    -- **El peu va amb els números**: diu quan s'ha cregut el cel, i és un número que canvia sol (docs/30, docs/60).
    if footer ~= nil then footer.text = the_footer() end
    ui.present('gl16')
  end
  if now - last_sky >= kSkyMs then
    last_sky = now
    if sky_canvas ~= nil then
      paint_the_sky(sky_canvas)
      sky_canvas:invalidate()
    end
    ui.present('gl16')
  end
  ui.present()
end

function on_resize(w, h)
  wid, hei = w, h
  ui.clear(15)
  if sky_canvas ~= nil then sky_canvas:release() end
  if info_canvas ~= nil then info_canvas:release() end
  sky_canvas, info_canvas = nil, nil
  build()
  layout()
  read_the_sky()
  refresh_the_words()
  if sky_canvas ~= nil then paint_the_sky(sky_canvas) end
  if info_canvas ~= nil then paint_the_numbers(info_canvas) end
  ui.present()
end

function on_pause() save_state() end
function on_save() save_state() end
function on_close()
  save_state()
  -- **El mòdul no s'apaga si el sistema el necessita** (0.192.63): amb el commutador de Settings encès el rellotge es creu el cel i el
  -- receptor ha de seguir engegat encara que aquesta pàgina se'n vagi; el mòdul és del sistema i no de la pàgina (docs/30, docs/50).
  local ok, sync = pcall(cfg.gps_sync)
  if ok and sync == true then return end
  pcall(gps.off)
end
