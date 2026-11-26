-- lora: **el monitor de l'aire, que només mira** (0.192.49, l'amo: *«Fer una app Monitor/Sniffer LORA. Ha de buscar senyals»*).
--
-- Aquesta pàgina fa dues coses i les fa en l'ordre en què un monitor les ha de fer:
--
--   · **escombra una banda** —de 863 a 870 MHz, de 433,05 a 434,79 o de 902 a 928— i mesura **quanta energia hi ha** a cada canal
--     amb l'RSSI instantani del SX1262 (`lora.sniff()` i `lora.rssi()`), que és el que un analitzador d'espectre ensenya. El que
--     es veu són les barres del canvas, el terra de soroll i el canal on hi ha més senyal;
--   · **escolta el canal més fort** que ha trobat l'escombrat, i hi prova els sis factors d'expansió (7 a 12) de dos en dos
--     segons, perquè un paquet de LoRa només arriba a qui l'escolta amb el SF amb què va sortir. El que arriba s'apunta amb la
--     seva hora, la seva potència, el seu SNR i les seves dades.
--
-- **I no transmet mai** (l'amo: *«Ha de buscar senyals»*): la ràdio que aquest sistema exposa no té cap porta d'emetre, i això
-- no és una limitació de la pàgina sinó del servei sencer —una ràdio que només escolta es pot deixar encesa sense permís de
-- ningú i no pot tapar la veu de ningú altre (docs/30, docs/50).
--
-- **Els números són els del xip i no una traducció**: l'RSSI arriba en dècimes de dB i l'SNR en quarts, i aquesta pàgina els
-- divideix en el moment d'escriure'ls. Una pàgina que arrodonís a l'entrada perdria el mig dB que distingeix un node d'un altre
-- (docs/30).

local caps = sys.caps()
if not caps.display then
  console.print('lora: this board has no glass')
  return
end

-- **`v1`**: la primera forma d'aquesta pàgina; una forma d'una altra versió es llença (docs/30, docs/45).
local kLayout = 'v1'
local kStateBytes = 512

local MARGIN = 14
local TITLE_H = 46
local SUB_H = 22
local TABS_H = 44
local GAP = 10
local NOTE_H = 22

-- **Les tres bandes que aquest monitor coneix**, amb el nom amb què les anomena una persona i no el número: la de 868 és la
-- d'Europa, la de 433 és la dels mandos i els sensors de casa, i la de 902 a 928 la d'Amèrica. **L'app no decideix què és legal
-- en cada país** —això és de qui la fa anar— i el que fa és dir en quina banda està escoltant (docs/30, docs/50).
local BANDS = {
  -- **El pas és el de mig amplada de banda**: un receptor de 125 kHz mira ±62,5 kHz al seu voltant, o sigui que dos canals
  -- separats per 200 kHz deixen un forat de 75 kHz al mig on una portadora no la veu ningú (vist a l'emulador: els tres nodes
  -- de 868,1, 868,3 i 868,5 no hi eren). Amb 100 kHz el que s'escombra es toca i el que hi ha entremig es veu (docs/50).
  { name = 'EU 868', from = 863000000, to = 870000000, step = 100000 },
  { name = 'EU 433', from = 433050000, to = 434790000, step = 100000 },
  { name = 'US 915', from = 902000000, to = 928000000, step = 200000 },
}

-- **El terra de soroll i el que un canal ha de pujar per sobre perquè valgui la pena escoltar-lo**: sis dB. Per sota d'això el
-- que hi ha és el mateix soroll de tots els canals, i triar-lo seria triar l'atzar (docs/30).
local kAboveTheFloorDb = 6

-- **Un escombrat reparteix els canals entre les voltes del bucle** (0.192.49): cada mesura és un canvi de mode del xip i el
-- temps d'assentament de l'AGC, o sigui que fer-ne quaranta dins una sola volta seria mig segon sense que el sistema respongui a
-- un dit. Sis canals per volta són un escombrat que es veu acabar i un vidre que no es queda sense mans (docs/30, docs/50).
local kChannelsPerTurn = 6

-- **Els segons que es dediquen a cada factor d'expansió** quan s'escolta: dos, que és el que triga un paquet lent a arribar i el
-- que fa que els sis SF del recorregut costin dotze segons (docs/50).
local kSfDwellMs = 2000
local kSfs = { 7, 8, 9, 10, 11, 12 }

-- **La paraula de sincronisme que s'espera**: la pública (`0x34`, la de LoRaWAN), que és la que fa servir gairebé tot el que
-- parla LoRa a l'aire lliure (docs/50).
local kSync = 0x34

local TABS = { 'Spectrum', 'Packets' }

local the_band = 1
local the_tab = 1
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

local function save_state()
  local block = the_state()
  if block == nil then return false end
  block:clear()
  block:write(0, string.format('%s\ntab=%d\nband=%d\n', kLayout, the_tab, the_band))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return end
  local text = block:read(0, kStateBytes) or ''
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  if text:match('^([^\n]*)') ~= kLayout then return end
  local tab = tonumber(text:match('tab=(%-?%d+)'))
  local band = tonumber(text:match('band=(%-?%d+)'))
  if tab ~= nil and tab >= 1 and tab <= #TABS then the_tab = math.floor(tab) end
  if band ~= nil and band >= 1 and band <= #BANDS then the_band = math.floor(band) end
end

-- ------------------------------------------------------------------ els números, dits com els diu un monitor

-- **L'RSSI en dècimes de dB, escrit amb un decimal**: `-104.3 dBm`. Un enter perdria el mig dB que separa dos nodes (docs/30).
local function dbm(tenths)
  local sign = ''
  local value = tenths
  if value < 0 then
    sign = '-'
    value = -value
  end
  return string.format('%s%d.%d dBm', sign, value // 10, value % 10)
end

-- **L'SNR en quarts de dB**: `6.5 dB`, i el signe hi és perquè un SNR negatiu és el que diu que la senyal és més fluixa que el
-- soroll, que és on LoRa encara treballa (docs/50).
local function snr(quarters)
  local sign = ''
  local value = quarters
  if value < 0 then
    sign = '-'
    value = -value
  end
  return string.format('%s%d.%d dB', sign, value // 4, (value % 4) * 25 // 10)
end

-- **L'hora de la persona quan la placa en té, i l'uptime quan no** (docs/30, docs/60): un paquet sense hora no es pot creuar amb
-- res, i una placa sense pila no ha d'inventar-se'n cap.
local function the_clock()
  local ok, now = pcall(time.date)
  if ok and type(now) == 'table' and now.hour ~= nil then
    return string.format('%02d:%02d:%02d', now.hour, now.min or 0, now.sec or 0)
  end
  local ms = sys.time_ms() or 0
  return string.format('%02d:%02d', (ms // 60000) % 60, (ms // 1000) % 60)
end

local function mhz(hz)
  return string.format('%d.%03d', hz // 1000000, (hz % 1000000) // 1000)
end

local function human_bytes(n)
  if n == nil then return '-' end
  if n < 1024 then return string.format('%d B', n) end
  return string.format('%.1f KB', n / 1024)
end

-- **Un paquet, dit de dues maneres**: les dades tal com són si són text llegible —que és el que gairebé tots són— i en
-- hexadecimal si no ho són. Una pàgina que ensenyi bytes crus d'una frase no serveix a ningú, i una que amagui el que no entén
-- amaga el que hi ha (docs/30).
local function words_of(data)
  local printable = true
  for i = 1, #data do
    local byte = data:byte(i)
    if byte < 32 or byte > 126 then
      printable = false
      break
    end
  end
  if printable and #data > 0 then return data end
  local out = {}
  for i = 1, math.min(#data, 48) do
    out[#out + 1] = string.format('%02x', data:byte(i))
  end
  if #data > 48 then out[#out + 1] = '...' end
  return table.concat(out, ' ')
end

-- ------------------------------------------------------------------ el que la ràdio ha sentit

-- **La memòria d'aquesta pàgina**: el que ha mesurat l'escombrat i el que ha arribat escoltant. No va a `state` —una forma que
-- torna ha de dir on era la persona, i l'aire d'ara mateix és dada i no forma— i es perd en tancar, que és el que ha de fer un
-- monitor: el que s'emporta algú és una foto i no un registre (docs/30, docs/45).
local spectrum = {}        -- una entrada per canal: { hz = ..., dbm = ... }
local strongest = nil      -- l'índex del canal més fort, o nil
local packets = {}         -- el més nou al principi
local kMaxPackets = 40
local mode = 'stopped'     -- 'scanning', 'listening' o 'stopped'
local listen_band = 1
local note = 'press Scan to look at the air'
local sweeping = 0         -- el canal per on va l'escombrat
local sweep_band = 1       -- la banda que s'està escombrant, que no és la triada si se'n canvia a mig camí
local sf_index = 1
local dwell_from = 0
local radio_ok = false
-- **El que fa que la pàgina marxi** (0.192.49): el `Close` del menú aixeca aquesta bandera i el bucle acaba, que és el camí que
-- passa pel final d'aquest fitxer —els controls es tornen, el paper es renta i la ràdio queda en repòs—. `app.exit()` també
-- funciona des d'un ganxo des de la 0.192.49, però marxaria sense passar per aquí (docs/30, docs/90).
local quitting = false

local function the_band_now() return BANDS[the_band] end

local function channels_of(band)
  local out = {}
  local hz = band.from
  while hz <= band.to do
    out[#out + 1] = hz
    hz = hz + band.step
  end
  return out
end

-- **El terra de soroll és el més fluix de la banda i no una constant**: un receptor amb el seu soroll propi i una banda amb el
-- seu moviment, i l'única cosa honesta és comparar cada canal amb el que aquesta banda té a baix (docs/30).
local function the_floor()
  local lowest = nil
  for i = 1, #spectrum do
    local dbm10 = spectrum[i].dbm
    if lowest == nil or dbm10 < lowest then lowest = dbm10 end
  end
  if lowest == nil then return -1250 end
  return lowest
end

local function find_the_strongest()
  local floor = the_floor()
  local best = nil
  for i = 1, #spectrum do
    local dbm10 = spectrum[i].dbm
    if dbm10 - floor >= kAboveTheFloorDb * 10 then
      if best == nil or dbm10 > spectrum[best].dbm then best = i end
    end
  end
  return best
end

-- ------------------------------------------------------------------ la pàgina

local title, subtitle, tabs, bar, spectrum_view, packets_list, note_line
local wid, hei = 0, 0

local owned = {}

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
  spectrum_view, packets_list = nil, nil
end

-- **El dibuix de l'espectre**: una barra per canal, l'escala a l'esquerra i el terra de soroll com una línia. El que es pinta és
-- el que s'ha mesurat i no una interpolació: un canal que no s'ha mesurat no té barra (docs/30, docs/45).
local function paint_the_spectrum(canvas)
  local box = canvas:caps()
  local scale = 2
  local lh = ui.line_height(scale)
  canvas:clear(15)
  local band = BANDS[listen_band] or the_band_now()

  if #spectrum == 0 then
    canvas:text(0, 0, 'no sweep yet', scale, 8)
    canvas:text(0, lh + 6, 'File > Scan measures every channel of', scale, 8)
    canvas:text(0, 2 * lh + 12, band.name .. ' and draws what it hears.', scale, 8)
    return
  end

  -- **L'escala és la de la finestra i el seu sostre és el rang del xip**: de −125 a −55 dBm, que és on cauen totes les senyals
  -- que un SX1262 pot rebre. Una barra que arribi al sostre vol dir «més fort que això» i no un número inventat (docs/30).
  local low = -1250
  local high = -550
  local top = lh + 8
  local floor = the_floor()
  local plot_h = box.height - top - lh - 8
  if plot_h < 40 then plot_h = 40 end

  local function row_of(dbm10)
    local value = dbm10
    if value < low then value = low end
    if value > high then value = high end
    return top + plot_h - ((value - low) * plot_h) // (high - low)
  end

  -- El terra de soroll: una línia que diu on és el que no és de ningú.
  local floor_y = row_of(floor)
  for x = 0, box.width - 1, 6 do
    canvas:pixel(x, floor_y, 10)
    canvas:pixel(x + 1, floor_y, 10)
  end

  local count = #spectrum
  local slot = box.width / count
  local w = math.max(1, math.floor(slot) - 1)
  for i = 1, count do
    local x = math.floor((i - 1) * slot)
    local top_of_bar = row_of(spectrum[i].dbm)
    if top_of_bar < top + plot_h then
      local level = (i == strongest) and 0 or 4
      canvas:rect(x, top_of_bar, w, (top + plot_h) - top_of_bar, level)
      if i == strongest then canvas:frame(x, top_of_bar, w, (top + plot_h) - top_of_bar, 0) end
    end
  end

  -- **El sostre i el terra de l'escala, i la banda**: el que una persona ha de poder llegir és quant i on, i les dues coses
  -- costen una línia de text cada una (docs/30).
  canvas:text(0, 0, string.format('%s  %s to %s MHz', band.name, mhz(band.from), mhz(band.to)), scale, 0)
  -- **El sostre a dalt i el terra a baix, a la dreta del traç**: el que una persona ha de poder llegir és quant i on, i el
  -- número del revés que no es toca (docs/30).
  local top_words = string.format('%d dBm', high // 10)
  canvas:text(box.width - ui.text_width(top_words, scale), lh + 6, top_words, scale, 8)
  local floor_words = string.format('floor %s', dbm(floor))
  canvas:text(0, box.height - lh, floor_words, scale, 8)
  local low_words = string.format('%d dBm', low // 10)
  canvas:text(box.width - ui.text_width(low_words, scale), box.height - lh, low_words, scale, 8)
end

-- **La llista de paquets**: l'hora, la freqüència, el SF, l'RSSI i el SNR a la fila, i les dades a sota. El més nou al
-- principi, perquè el que interessa és el que acaba d'arribar (docs/30).
local function packet_rows()
  local out = {}
  for i = 1, #packets do
    local one = packets[i]
    -- **La fila cap en una línia**: the hour is on the right, with the size, and the line carries the channel, the SF and
    -- the two measures —a row that says more than it fits is a row that loses its last number (docs/30).
    -- **Què hi cap i què no**: la fila té dues línies i una columna a la dreta, i el que ha de quedar-se és el que un monitor
    -- busca —l'hora, el canal, el SF, la potència i l'SNR— amb la mida del paquet al costat del seu text. Una línia que digui
    -- més del que hi cap perd l'últim número i el que es llegeix és mitja mesura (docs/30).
    local detail = string.format('%s  SF%d  %s  %s%s', mhz(one.hz), one.sf, dbm(one.rssi), snr(one.snr),
                                 one.crc and '' or '  CRC BAD')
    out[#out + 1] = { title = string.format('%s  ·  %s', words_of(one.data), human_bytes(one.length)),
                      subtitle = detail, detail = one.at }
  end
  if #out == 0 then
    out[1] = { title = 'nothing heard yet', subtitle = 'a packet needs the same spreading factor as the listener', detail = '' }
  end
  return out
end

local function say_the_note()
  if note_line ~= nil then
    note_line.text = note
  end
end

-- ------------------------------------------------------------------ la disposició i les vistes

local function geometry()
  local g = { wide = wid > hei, width = wid - 2 * MARGIN }
  g.bar = ui.line_height(4) + 10
  g.top = g.bar + 3 * GAP + TITLE_H + SUB_H + TABS_H
  g.room = hei - g.top - MARGIN - NOTE_H - GAP
  if g.room < 120 then g.room = 120 end
  return g
end

local function layout()
  local g = geometry()
  if bar ~= nil then
    bar:resize(g.width, g.bar)
    bar:place(MARGIN, GAP // 2)
  end
  local y = g.bar + GAP
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
  if spectrum_view ~= nil then
    spectrum_view:resize(g.width, g.room)
    spectrum_view:place(MARGIN, g.top)
  end
  if packets_list ~= nil then
    packets_list:resize(g.width, g.room)
    packets_list:place(MARGIN, g.top)
  end
  if note_line ~= nil then
    note_line:resize(g.width, NOTE_H)
    note_line:place(MARGIN, hei - MARGIN - NOTE_H)
  end
  -- **La pestanya triada es diu a cada disposició** (0.192.49): una vista que es torna a fer —una banda nova, una tornada d'un
  -- son, un vidre que canvia de mida— ha de dir quina pestanya és la de debò, i el cercle el posa el sistema a la que es tria
  -- quan el focus hi arriba (docs/30, docs/90).
  if tabs ~= nil then tabs.index = the_tab end
end

local function build_the_view()
  local g = geometry()
  if the_tab == 1 then
    spectrum_view = keep(ui.canvas(g.width, g.room))
    if spectrum_view ~= nil then
      spectrum_view.on_draw = function(self) paint_the_spectrum(self) end
    end
    if subtitle ~= nil then
      subtitle.text = string.format('%s · %s', the_band_now().name,
                                    (mode == 'listening') and ('listening on ' .. mhz(BANDS[listen_band].from)) or
                                    (mode == 'scanning' and 'sweeping' or 'stopped'))
    end
  else
    packets_list = keep(ui.list(g.width, g.room))
    if packets_list ~= nil then
      packets_list.items = packet_rows()
      packets_list.row_height = math.max(58, ui.line_height(2) * 3)
      packets_list.scale = 2
    end
    if subtitle ~= nil then
      subtitle.text = string.format('%d packet(s) heard', #packets)
    end
  end
end

-- **El que la pàgina ha de tornar a dir, sense pintar-lo** (0.192.49): `ui.present` és l'última cosa d'un canvi de vista, i la
-- tira de pestanyes s'ha de tornar a dir abans que el present compongui la pàgina —si es pinta abans, el que hi ha al vidre és
-- la tira de la vista anterior (vist a l'emulador, docs/30, docs/90).
local function mark_the_page()
  if spectrum_view ~= nil then spectrum_view:invalidate() end
  if packets_list ~= nil then
    packets_list.items = packet_rows()
    -- **El compte de la capçalera es diu cada vegada**: una pàgina que diu «2 packet(s) heard» amb quatre files a sota és una
    -- pàgina que no ha mirat el que ensenya (docs/30).
    if subtitle ~= nil then subtitle.text = string.format('%d packet(s) heard', #packets) end
  end
  say_the_note()
end

local function repaint_the_page(quality)
  mark_the_page()
  ui.present(quality or 'gl16')
end

-- ------------------------------------------------------------------ el que la pàgina fa amb l'aire

local function stop_the_radio()
  if radio_ok then pcall(lora.idle) end
  mode = 'stopped'
  note = 'stopped: the radio is at rest'
  if subtitle ~= nil and the_tab == 1 then subtitle.text = the_band_now().name .. ' · stopped' end
end

local function start_scanning()
  if not radio_ok then
    note = 'this board has no LoRa radio'
    return
  end
  local band = the_band_now()
  spectrum = {}
  strongest = nil
  sweeping = 1
  sweep_band = the_band
  mode = 'scanning'
  listen_band = the_band
  note = string.format('sweeping %s, %s to %s MHz', band.name, mhz(band.from), mhz(band.to))
  -- **La modulació d'un escombrat és la de rebre i prou**: l'amplada de banda és el que decideix quants canals veu alhora, i
  -- 125 kHz és la de LoRa i la que dona una barra per canal (docs/50).
  pcall(lora.reception, { sf = 7, bw = 7, cr = 1, sync = kSync, preamble = 8 })
end

local function start_listening()
  if not radio_ok then
    note = 'this board has no LoRa radio'
    return
  end
  if strongest == nil then
    note = 'nothing above the noise: scan the band first'
    return
  end
  listen_band = the_band
  mode = 'listening'
  sf_index = 1
  dwell_from = sys.time_ms() or 0
  pcall(lora.frequency, spectrum[strongest].hz)
  pcall(lora.reception, { sf = kSfs[sf_index], bw = 7, cr = 1, sync = kSync, preamble = 8 })
  pcall(lora.listen, 0)
  note = string.format('listening on %s, SF%d', mhz(spectrum[strongest].hz), kSfs[sf_index])
end

-- **Una volta d'escombrat**: uns quants canals, i quan s'arriba al final es tria el més fort. El que es mesura és el nivell
-- instantani del canal amb l'antena posada a cada freqüència (docs/30, docs/50).
local function sweep_a_little()
  local band = BANDS[sweep_band] or the_band_now()
  local channels = channels_of(band)
  local from = sweeping
  local to = math.min(#channels, from + kChannelsPerTurn - 1)
  for i = from, to do
    -- **L'antena primer i la mesura després** (0.192.49): `rssi()` és el nivell del canal on el xip està posat, i sense el canvi
    -- de freqüència el que es mesuraria quaranta vegades és el mateix canal (vist a l'emulador: totes les barres iguals).
    pcall(lora.frequency, channels[i])
    pcall(lora.sniff)
    local ok, level = pcall(lora.rssi)
    if not ok or type(level) ~= 'number' then level = -1250 end
    spectrum[i] = { hz = channels[i], dbm = level }
  end
  sweeping = to + 1
  if sweeping > #channels then
    strongest = find_the_strongest()
    mode = 'stopped'
    if strongest ~= nil then
      note = string.format('%s is the loudest on %s: press Listen', dbm(spectrum[strongest].dbm), mhz(spectrum[strongest].hz))
      if subtitle ~= nil and the_tab == 1 then
        subtitle.text = string.format('%s · %s on %s', band.name, dbm(spectrum[strongest].dbm), mhz(spectrum[strongest].hz))
      end
    else
      note = string.format('nothing above the noise in %s', band.name)
    end
    repaint_the_page('gl16')
  end
end

-- **Escoltar**: un paquet només arriba amb el seu SF, i el que fa aquest bucle és donar dos segons a cada un i mirar si n'ha
-- vingut algun. Quan arriba, s'apunta, es diu al peu i es repinta la pàgina (docs/50).
local function listen_a_little()
  local at = sys.time_ms() or 0
  if at - dwell_from >= kSfDwellMs then
    dwell_from = at
    sf_index = sf_index + 1
    if sf_index > #kSfs then sf_index = 1 end
    pcall(lora.reception, { sf = kSfs[sf_index], bw = 7, cr = 1, sync = kSync, preamble = 8 })
    pcall(lora.listen, 0)
    if subtitle ~= nil and the_tab == 1 then
      subtitle.text = string.format('%s · listening %s, SF%d', the_band_now().name,
                                    mhz(spectrum[strongest].hz), kSfs[sf_index])
    end
    return false
  end
  local packet = lora.poll()
  if packet == nil then return false end
  packets[#packets + 1] = {
    at = the_clock(),
    hz = packet.hz,
    sf = kSfs[sf_index],
    rssi = packet.rssi,
    snr = packet.snr,
    data = packet.data,
    length = packet.length,
    crc = packet.crc,
  }
  while #packets > kMaxPackets do table.remove(packets, 1) end
  note = string.format('%d B from %s, %s', packet.length, mhz(packet.hz), dbm(packet.rssi))
  return true
end

-- ------------------------------------------------------------------ el menú

local function pick_the_band(index)
  the_band = index
  save_state()
  spectrum = {}
  strongest = nil
  mode = 'stopped'
  note = string.format('%s: press Scan', the_band_now().name)
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  -- **La tira es torna a dir després del rentat i del focus** (0.192.49): `ui.clear` pinta blanc a tota la pàgina i el que el
  -- sistema tenia marcat de la tira es perd amb ell, i el cercle només el torna a posar un `ui.focus` sobre la tira. Sense
  -- això, canviar de banda deixava la pestanya triada sense la inversió i la pàgina no deia on era (vist a l'emulador).
  if tabs ~= nil then ui.focus(tabs) end
  repaint_the_page('gl16')
end

local function choose_entry(entry)
  if entry == 1 then
    start_scanning()
    repaint_the_page('gl16')
  elseif entry == 2 then
    start_listening()
    repaint_the_page('gl16')
  elseif entry == 3 then
    stop_the_radio()
    repaint_the_page('gl16')
  elseif entry == 5 then
    -- **El `Close` d'un menú d'aquesta casa**: la ràdio va a repòs abans que la pàgina marxi, perquè el que deixa d'escoltar
    -- ha de ser el que apaga i no el nucli trobant-se una ràdio encesa d'una app que ja no hi és (docs/30, docs/50).
    stop_the_radio()
    save_state()
    quitting = true
    return
  end
end

local function make_the_bar(g)
  bar = ui.menu_bar(g.width, g.bar)
  if bar == nil then return end
  bar.bar_height = g.bar
  bar.items = {
    { title = 'File', items = {
      { label = 'Scan' },
      { label = 'Listen' },
      { label = 'Stop' },
      '-',
      { label = 'Close' },
    } },
    { title = 'Band', items = {
      { label = BANDS[1].name, checked = (the_band == 1) },
      { label = BANDS[2].name, checked = (the_band == 2) },
      { label = BANDS[3].name, checked = (the_band == 3) },
    } },
  }
  bar.on_pick = function(self, title_index, entry)
    if title_index == 1 then
      choose_entry(entry)
    elseif title_index == 2 then
      pick_the_band(entry)
    end
  end
end

local show_tab

local function build()
  local c = ui.caps()
  wid, hei = c.width, c.height
  ui.clear(15)
  local g = geometry()
  make_the_bar(g)
  title = ui.label(wid - 2 * MARGIN, TITLE_H)
  if title ~= nil then
    title.text = 'LoRa'
    title.scale = 4
    title.align = 'left'
  end
  subtitle = ui.label(wid - 2 * MARGIN, SUB_H)
  if subtitle ~= nil then
    subtitle.scale = 2
    subtitle.align = 'left'
    subtitle.color = 8
    subtitle.text = 'the air around this board'
  end
  tabs = ui.tabs(wid - 2 * MARGIN, TABS_H)
  if tabs ~= nil then
    tabs.items = TABS
    tabs.index = the_tab
    tabs.on_change = function(_, index) show_tab(index) end
  end
  note_line = ui.label(wid - 2 * MARGIN, NOTE_H)
  if note_line ~= nil then
    note_line.scale = 2
    note_line.align = 'left'
    note_line.color = 8
    note_line.text = note
  end
end

show_tab = function(index)
  if index == nil or index < 1 or index > #TABS then return end
  if index ~= the_tab then
    the_tab = index
    save_state()
    give_back_the_view()
    -- **El full es renta i la tira es torna a dir després** (0.192.49): `ui.clear` pinta blanc a tota la pàgina i el que el
    -- sistema havia marcat de la tira de pestanyes es perd amb ell, o sigui que el número de la pestanya triada es torna a
    -- escriure després del rentat i abans de compondre la pàgina (vist a l'emulador: la pestanya triada era la d'abans).
    if tabs ~= nil then tabs.index = 0 end
    ui.clear(15)
    build_the_view()
    layout()
    if tabs ~= nil then tabs.index = the_tab end
    mark_the_page()
  end
  if tabs ~= nil then
    tabs.index = the_tab
    ui.focus(tabs)
  end
  ui.present('gl16')
end

-- ------------------------------------------------------------------ l'app

load_state()
build()
build_the_view()
layout()
say_the_note()
ui.focus(tabs)
ui.present()

-- **La ràdio s'ha de poder demanar abans de res**: si aquesta placa no en té, la pàgina ho diu i no fingeix un aire buit. El
-- `begin` és idempotent i el nucli el pot haver fet ja (docs/30, docs/50).
if lora ~= nil then
  local ok, err = pcall(lora.begin)
  radio_ok = ok and (err ~= false)
  if not radio_ok then note = 'the radio did not answer' end
else
  note = 'this board has no LoRa radio'
end
say_the_note()
if not radio_ok then repaint_the_page('gl16') end

function on_resume()
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  if tabs ~= nil then ui.focus(tabs) end
  repaint_the_page('gl16')
end

function on_resize(w, h)
  wid, hei = w, h
  give_back_the_view()
  ui.clear(15)
  build_the_view()
  layout()
  if tabs ~= nil then ui.focus(tabs) end
  repaint_the_page('gl16')
end

-- **Una app que va a segon pla no deixa la ràdio encesa** (0.192.49): el xip en recepció gasta mil·liampers i el que hi ha
-- darrere d'una targeta no s'està mirant; el que torna a escoltar és el dit que torna a prémer `Listen` (docs/30, docs/50).
function on_pause()
  stop_the_radio()
  save_state()
end
function on_save() save_state() end
function on_close()
  stop_the_radio()
  save_state()
end

-- **El bucle**: un tros d'escombrat, dos segons d'escolta, i la resta a deixar girar el nucli. El que no es fa és tornar a
-- pintar una pàgina que no ha canviat: aquest vidre no anima res i cada passada paga la seva forma d'ona (docs/30, docs/85).
--
-- **I mentres un menú és desplegat no es mesura res** (0.192.49): la targeta del menú es pinta als píxels de la pàgina i un
-- escombrat que s'acabés ara la repintaria a sota, o sigui que la targeta desapareixeria amb el primer repàs. El que es fa és
-- esperar que es plegui —el sistema plega el menú quan una entrada es tria o quan el dit toca fora— i tornar-hi després.
local open_was = 0
while not quitting do
  ui.present()
  -- **La inversió de la tira es torna a dir si algú l'ha moguda** (0.192.49): el cercle del sistema camina per les parades de la
  -- pàgina i el menú el pot deixar en una altra, i una pestanya que diu una cosa mentres la pàgina n'ensenya una altra és el
  -- pitjor que pot fer una tira. Escriure-hi el número que toca només quan difereix no costa cap píxel quan ja hi és.
  if quitting then break end
  local open = (bar ~= nil) and (bar.open or 0) or 0
  if open ~= open_was then
    -- **Un menú que s'obre no es repinta a sota** (0.192.49): la targeta del sistema es pinta als píxels de la pàgina i el
    -- sistema ja ha compost el que hi havia davant seu; tornar a dibuixar el canvas ara el posaria a sobre de la targeta i el
    -- que es llegiria seria una targeta tallada per la meitat. El que cal fer és el contrari —reposar el canvas quan la targeta
    -- s'ha plegat—, i mentrestant la barra té tota l'alçada perquè el desplegable hi càpiga (docs/30, docs/90).
    open_was = open
    if bar ~= nil then bar:resize(wid - 2 * MARGIN, (open > 0) and hei or geometry().bar) end
    if open == 0 then repaint_the_page('gl16') end
  elseif open == 0 then
    if mode == 'scanning' then
      sweep_a_little()
    elseif mode == 'listening' then
      if listen_a_little() then repaint_the_page('gl16') end
    end
  end
  sys.delay(50):await()
end

-- **Una pàgina que marxa deixa paper net** (0.192.49): els controls primer, perquè una barra que encara deu píxels es pinta a sobre
-- del rentat, i la ràdio ja és en repòs des del `Close` (docs/30, docs/90).
stop_the_radio()
local kids = ui.root().children
for i = #kids, 1, -1 do kids[i]:release() end
ui.clear(15)
ui.present('gld16')
console.print('lora: closed')
