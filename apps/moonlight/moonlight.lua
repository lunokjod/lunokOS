--[[
moonlight: **la botiga de descàrregues de lunokOS** (0.200.0, l'amo: *«Farem la botiga de descarregues de lunokOS. Li
direm "Moonlight Installer"... Et baixes la llista d'arxius, et baixes el manifesto.json i vas baixant les icones. Mostraràs
una llista d'apps amb la seva icona en una llista, sobre la llista una caixa de text de cerca, amb la seva lupa i tota la
pesca. Teclejar en aquella caixa fa que la llista es redueixi fins trobar l'app buscada. Si toques una app de la llista vas
a una vista de detalls on tens tota la info de l'app. I el botó "Install" si l'app no està al sistema, "Uninstall" si està i
"Upgrade" si la versió es mes gran. En cas de tenir una app d'una versió mes avançada que la del repo haurás de marcar-la
amb un fons diferent»*).

DUES PÀGINES I UNA COSA AL DARRERE:

  · **la llista** — el títol, un `Refresh`, la caixa de cerca amb la seva lupa, les files de les apps (la icona, el nom i
    la versió que hi ha aquí i la que hi ha al dipòsit) i el paginador. Escriure a la caixa **redueix la llista**;
  · **la fitxa** — la icona gran, el nom, l'id i l'autor, les dues versions, les plaques i la descripció sencera, amb
    **un sol botó** que és el que toca: `Install` si no hi és, `Upgrade` si la del dipòsit és més nova, `Uninstall` si hi és.

I al darrere `store.lua`, que és qui parla amb github.com (el que fa la comanda `repo`), i una **cua de feina** que el
bucle buida **una app per torn**: baixar un manifest i la seva icona pot trigar un segon i quaranta-una apps són quaranta-un
torns, i la pàgina s'ha de poder tocar mentre això passa (docs/30, docs/85).

**DUES COSES QUE AQUESTA PÀGINA FA I QUE NO ES VEUEN** (docs/30, docs/45, docs/90):

 1. **no es torna a construir la pàgina mentre es tecleja**: la caixa de text té el focus del teclat i el cursor, i
    alliberar-la a cada lletra seria perdre el cursor i tornar a demanar el teclat. El que es refà és **la llista** i prou
    (`refilter`), i una resposta que arriba del dipòsit **toca les etiquetes que ja hi són** (`update_rows`) en lloc de
    tornar a fer les files: cada fila té una icona, i una icona és una descodificació de PNG (docs/75).
 2. **una app que és més avançada aquí que al dipòsit porta el fons gris** (`kNewer`): és l'única manera que aquest vidre té
    de dir-ho sense una paraula més, i és el que l'amo ha demanat.

**Una app d'administrador** (el manifest diu `"admin": true`): the system lets it write where the prompt writes, and that is
what makes install and uninstall possible from the glass (docs/40, docs/70).
]]

local store = require('store')

-- **La lupa és una marca de veritat i no un cercle dibuixat a mà** (0.200.0, l'amo: *«Lo de l'esquerra de la caixa de cerca es
-- una lupa? no es veu, es massa fina, busca una bon icona de lupa a la font que tenim d'awesome»*): `res/marks.ttf` és un
-- glif de FontAwesome —el de la lupa, U+F002— escrit amb `tools/make_keys.py` en un byte (`\1`), que és el que una app pot
-- escriure en un text d'aquest sistema: el dibuix el fa la cara i no una taula de coordenades meva (docs/30, docs/45).
local marks_face, marks_why = ui.load_font('/apps/moonlight/res/marks.ttf')
if marks_face == nil then console.print('moonlight: res/marks.ttf: ' .. tostring(marks_why)) end
local kLens = '\1'

local function say(t) console.print('moonlight: ' .. t) end

-- The sizes of the paper project in pixels of line, asked of the face the system draws with (docs/45 § 3).
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
-- **L'estat va un pas més baix que la versió** (0.200.0, l'amo: *«l'«up to date» al costat de la versió (i en text
-- petit)»*): the version is a number a person looks for and the state is a note about it, so the note is a size smaller
-- and it sits at the version's side instead of at the other end of the row.
local kTiny = scale_for(26)

local Margin = 12
local Gap = 8
local INK, PAPER = 0, 15
-- **El fons d'una fila que és d'una versió més avançada que la del dipòsit** (0.200.0): a light grey and not the paper,
-- which is the only way this glass has to say "this one is not like the others" with no other word (docs/30, docs/90).
local kNewer = 13
-- **Una marca apagada és mig gris i no un to més clar** (0.200.0): the circular arrow of `Refresh` is drawn in level five while
-- the shop does not have to ask the repository yet, and in ink when it does (docs/30, docs/90).
-- **Una app que ja hi és, en un to més clar** (0.200.0, l'amo: *«Les apps instal·lades han de tenir un color diferent (text una
-- mica mes clar)»*): level five is a mid grey that reads on the paper and is not the ink of the ones that are not here. A row
-- with the grey background —the one that is *newer here*— keeps its ink, because grey on grey is a word nobody reads.
local kInstalled = 5
local kDim = 5
-- **Els tres tons de la clau de la fitxa** (0.200.1): the sixteen levels of this glass are the paper at fifteen and the ink at
-- zero, o sigui que `Install` es queda el paper, `Upgrade` un graó per sota i `Uninstall` baixa fins al mig —el to que el dit
-- troba sense buscar-lo i que el dial no ha de ser el primer a oferir (docs/30, docs/45, docs/90).
local kToneIn = 15
local kToneUp = 13
local kToneOut = 9
-- La clau de la fletxa: un quadrat del cantó del dibuix de la marca, com el del panell de la barra.
local kAskW = 64
local kAskH = 48
-- **L'alçada de la targeta de la pregunta** (0.200.0): the card measures `height / 7` for its doors and the rest is the title,
-- the message and the note. **Cinc-cents** (0.200.1, l'amo: *«Botons mes alts»*): les dues portes volen setanta-un píxels
-- d'alçada —the most the kernel gives a card this tall—, the message fits at the body size and the note keeps a line under it
-- (docs/30, docs/90).
local kAskCardH = 500
-- **El dipòsit es pregunta cada dues hores** (0.200.0, l'amo: *«una fletxa circular i deshabilitada per ara (ja es refresca al
-- iniciar, i lo ideal seria esperar 2h per habilitar el refresc)»*): the age of the cache —the `mtime` of the index, which the
-- system keeps since 0.199.0— is what decides, and a clock nobody set means "ask", never "wait" (docs/70).
local kRefreshEvery = 2 * 3600
-- **Les files són gairebé fitxes** (0.200.0, l'amo: *«Les icones queden tallades a la llista! Has de fer les files mes altes,
-- quasi fitxes»*): the icon takes the first floor and it is **ninety-six pixels, the size of the drawing an app ships**, so it
-- is not a ninety-four-pixel picture squeezed into sixty and it cannot look cut. The title sits beside it and **under the
-- icon** goes the second line at the whole row's width —the version touching the icon's side and the state glued to it a size
-- smaller—, which is what the owner drew. **Cent quaranta-vuit i no cent cinquanta-sis**: the strip of the pages at the foot of
-- the list takes forty-one pixels and with a hundred and fifty-six that band cost a whole row —three per page instead of four—,
-- which is what `list_page_of` says and what the counter of the page showed.
local kRow = 148
local kIcon = 96
local kIconBig = 96
-- **L'amplada d'un botó es mesura i no s'endevina, i es mesura en pintar** (0.200.0): the first run of the page on the
-- emulator showed `R...`, `B...` and `N...` —a button of a hundred and four pixels holds four letters at the body size of this
-- face, not seven—, and a second run said `Refre...` because the measure had been taken **when the app was loaded**, when the
-- face of the system was not the one the page draws with. `ui.text_width` is the same measure the face draws with, and the
-- one that counts is the one asked for while the page is being built (docs/30, docs/45).
local function a_width_of(text, scale, least)
  -- **Trenta-dos píxels són del marc i no de la paraula**: la clau d'un botó dona setze píxels de marge a cada banda
  -- (`text_left = x + 16`, `text_width = width - 32`, docs/30) i una amplada de paraula més vint-i-vuit el tallava pels
  -- quatre píxels que falten —`Refre...`—. Quaranta deixen la paraula sencera amb vuit de aire.
  local said = math.floor(ui.text_width(text, scale)) + 40
  return (said < least) and least or said
end

local has_glass = sys.caps().display and true or false
local wid, hei = 0, 0
if has_glass then
  local c = ui.caps()
  wid, hei = c.width, c.height
end

-- ---------------------------------------------------------------- la forma que es recorda

local kLayout = 'v3'   -- la forma de la vista: `first` és una **fila** i `shot` la captura de la galeria (0.200.2, 0.200.5)
local kStateBytes = 1024
local state_block = nil

local page = 'list'    -- 'list', 'detail' o 'gallery'
local query = ''       -- el que hi ha a la caixa de cerca
local chosen = ''      -- l'app de la fitxa
local shot_ = 1        -- quina captura de la galeria es veu (1 a n)
local first_ = 1       -- la primera fila de la llista que es veu (0.200.2: era la pàgina)
local focus_ = ''      -- el nom del control on era el cercle
local asked = nil      -- el que ha demanat un ganxo i fa el bucle
local wanted = ''      -- l'app que demana el botó de la fitxa
local asking = false   -- la targeta del «Are you sure?» és a sobre la fitxa
-- **El que fa la barra mentre una feina és pel mig** (0.200.2): 0..100 quan se sap quant queda i -1 quan no —el número que
-- `ui.progress` pinta com «encara no ho sé»—, i `nil` vol dir que no hi ha res a esperar i la barra no hi és (docs/30).
local progress_ = nil
local painted_ = ''

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
  block:write(0, string.format('layout=%s\npage=%s\nfirst=%d\nchosen=%s\nshot=%d\nquery=%s\n',
                               kLayout, page, first_, chosen, shot_, (query:gsub('[\r\n]', ' '))))
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
  if at.layout ~= kLayout then return end
  if at.page == 'detail' then page = 'detail' end
  if at.page == 'gallery' then page = 'gallery' end
  local k = tonumber(at.shot)
  if k ~= nil and k >= 1 then shot_ = math.floor(k) end
  local n = tonumber(at.first)
  if n ~= nil and n >= 1 then first_ = math.floor(n) end
  if at.chosen ~= nil then chosen = at.chosen end
  if at.query ~= nil then query = at.query end
end

-- ---------------------------------------------------------------- el que se sap del dipòsit i del disc

local repo = {}       -- id -> { id, title, version, icon, author, boards, says } del dipòsit
local have = {}       -- id -> { version, title, icon } del que aquesta placa té
local status_ = ''    -- l'última cosa que l'app ha dit, a la línia de sota
local busy = false    -- una feina del botó (instal·lar, treure) és pel mig
local queue = {}      -- les apps que falten per demanar al dipòsit
local queue_total = 0 -- quantes eren quan es va preguntar, que és el que la barra compta
local queue_done = 0  -- quantes se n'han demanat

local function installed_now() have = store.installed() end

local function repo_count()
  local n = 0
  for _ in pairs(repo) do n = n + 1 end
  return n
end

-- **En quin estat és una app** (0.200.0): `not here` si no hi és, `here only` si el dipòsit no la té, i si hi és a tots
-- dos llocs la compara el `store.compare`, que compta els números i no les lletres (docs/40).
local function state_of(id)
  local here = have[id]
  local there = repo[id]
  local mine = (here ~= nil) and (here.version or '') or ''
  local theirs = (there ~= nil) and (there.version or '') or ''
  if here == nil then return 'not here', theirs end
  if there == nil then return 'here only', mine end
  local order = store.compare(theirs, mine)
  if order > 0 then return 'upgrade to ' .. theirs, mine end
  if order < 0 then return 'newer here', mine end
  return 'up to date', mine
end

local function title_of(id)
  local there = repo[id]
  if there ~= nil and there.title ~= nil and there.title ~= '' then return there.title end
  local here = have[id]
  if here ~= nil and here.title ~= nil and here.title ~= '' then return here.title end
  return id
end

-- La icona d'una app: **la del dipòsit si ja és al cau** (el mateix dibuix que la del manifest, i per això es prefereix)
-- i si no la que aquesta placa té muntada (docs/70).
local function icon_of(id)
  local file = store.icon_path(id)
  if fs.stat(file) ~= nil then return file end
  local here = have[id]
  if here ~= nil and here.icon ~= nil and here.icon ~= '' then return here.icon end
  return nil
end

-- **La llista, filtrada per la caixa de cerca**: el títol o l'id, i les majúscules no compten —qui escriu `jelly` vol
-- trobar `Jellyfin`—. Es fa cada cop que la caixa canvia i cada cop que arriba una app del dipòsit (docs/30).
local function visible()
  local needle = string.lower(query)
  local rows, seen = {}, {}
  local function one(id)
    local title = title_of(id)
    if needle ~= '' then
      local in_id = string.find(string.lower(id), needle, 1, true) ~= nil
      local in_title = string.find(string.lower(title), needle, 1, true) ~= nil
      if not in_id and not in_title then return end
    end
    local state, version = state_of(id)
    rows[#rows + 1] = { id = id, title = title, state = state, version = version }
  end
  for id in pairs(repo) do
    if not seen[id] then seen[id] = true one(id) end
  end
  for id in pairs(have) do
    if not seen[id] then seen[id] = true one(id) end
  end
  table.sort(rows, function(a, b)
    local left, right = string.lower(a.title), string.lower(b.title)
    if left == right then return a.id < b.id end
    return left < right
  end)
  return rows
end

-- **Si toca preguntar el dipòsit** (0.200.0): the age of the cache is the `mtime` of the index. A file that is not there, a
-- `mtime` of zero —which means nobody knows, not "very old"— and a board whose clock was never set all answer `true`: a shop
-- that cannot tell is a shop that asks.
local function may_refresh()
  local info = fs.stat(store.index_path())
  if info == nil then return true end
  local wrote = tonumber(info.mtime) or 0
  if wrote <= 0 then return true end
  local now = time.now()
  if now == nil or now <= 0 then return true end
  return (now - wrote) >= kRefreshEvery
end

-- Com de vell és el que se sap, dit amb paraules: una marca apagada sense dir per què és una marca que sembla espatllada.
local function asked_age()
  local info = fs.stat(store.index_path())
  if info == nil then return 'the repository has not been asked yet' end
  local wrote = tonumber(info.mtime) or 0
  local now = time.now()
  if wrote <= 0 or now == nil or now <= 0 then return '' end
  local secs = now - wrote
  if secs < 0 then secs = 0 end
  if secs < 120 then return 'asked just now' end
  if secs < 3600 then return string.format('asked %d min ago', secs // 60) end
  return string.format('asked %.1f h ago', secs / 3600)
end

-- ---------------------------------------------------------------- els controls de la pàgina

local owned = {}
local named = {}
local rows_shown = {}
local since_build = 0   -- quantes respostes han arribat des de l'última vegada que es van fer les files

local function keep(control)
  if control ~= nil then owned[#owned + 1] = control end
  return control
end

local function let_go(control)
  if control == nil then return end
  for i = #owned, 1, -1 do
    if owned[i] == control then table.remove(owned, i) end
  end
  pcall(function() control:release() end)
end

local function give_back()
  for i = #owned, 1, -1 do
    pcall(function() owned[i]:release() end)
  end
  owned, named, rows_shown = {}, {}, {}
end

local function a_label(text, w, scale, ink)
  local label = keep(ui.label(w, ui.line_height(scale)))
  if label ~= nil then
    label.text = text
    label.scale = scale
    label.color = ink or INK
  end
  return label
end

local function a_button(text, w, h, scale, on_click, primary)
  local button = keep(ui.button(w, h))
  if button == nil then return nil end
  button.title = text
  button.scale = scale
  button.primary = primary or false
  button.on_click = on_click
  return button
end

local function draw_now()
  if has_glass then ui.present('gl16') end
end

-- **La línia de sota la pàgina és on l'app diu què està fent** (0.200.0), i es pinta quan canvia: una descàrrega de mig
-- segon sense cap paraula és una pàgina que sembla morta (docs/30). `quiet` és per al torn de la cua, que ja pinta la
-- llista tot seguit i no cal que pinti dues vegades.
local function the_status(text, quiet)
  status_ = tostring(text or '')
  say(status_)
  if named.status_line ~= nil then
    named.status_line.text = status_
    if not quiet then draw_now() end
  end
end

-- **La barra de la feina** (0.200.2, l'amo: *«Tens una barra de progrés per la instalació/desinstalar/actualitzar oi que si???
-- I mentre esperem: botons deshabilitats (install/uninstall/update) fins acabar el procés»*): `ui.progress` amb el número que
-- se sap —els fulls de la cua són un compte conegut— i **un negatiu quan no**: una baixada d'un fitxer no diu quants en falten
-- fins que la llista de la carpeta arriba, i una barra que menteix un percentatge és pitjor que una barra que diu que no ho sap
-- (docs/30). `nil` vol dir que no hi ha cap feina i la barra no es fa.
local function the_progress(value)
  progress_ = value
  if not has_glass then return end
  local bar = named.progress
  if bar == nil then return end
  local shown = (value == nil) and 0 or math.max(-1, math.min(100, math.floor(value)))
  bar.value = shown
  ui.present('gl16')
end

-- **La lupa, amb la cara de les marques** (0.200.0): a `ui.label` of the mark and not a canvas of two circles, because a
-- ring of one pixel on this glass is a smudge and the glyph of FontAwesome is drawn by the face at any size. `kLensScale`
-- is the body that makes it as tall as the search box's line (the glyph is a 1664-unit mark in a 1000-unit em).
local kLensScale = 4

local function a_magnifier(w, h)
  local lens = keep(ui.label(w, h))
  if lens == nil then return nil end
  if marks_face ~= nil then lens:font(marks_face) end
  lens.text = kLens
  lens.scale = kLensScale
  lens.color = INK
  lens.align = 'center'
  lens.valign = 'middle'
  return lens
end

-- ---------------------------------------------------------------- la pàgina: la llista

local function list_geometry()
  local top = Margin + ui.line_height(kTitle) + Gap + ui.line_height(kBody) + 8 + Gap
  local pager_h = ui.line_height(kBody) + 16
  local pager_y = hei - Margin - ui.line_height(kSmall) - Gap - pager_h
  local per = math.max(1, (pager_y - Gap - top) // kRow)
  return top, pager_y, per
end


--[[
**UNA FILA SÍ I UNA NO AL REVÉS** (0.200.0, l'amo: *«Pots fer que cada fila vagi al revés de l'anterior?»*): odd rows have the
icon on the left and the words on its right, and the even ones are the same row turned round —the icon on the right, the title
on the left, the version and the state swapped with it—, so a list of forty apps reads as a shelf and not as a column:

     [icona] [títol]                 [títol] [icona]
     [versió] [estat]                [estat] [versió]

The two lines of the second row are **two halves of the row** and not a pair of texts one after the other: a half each keeps
every row the same width when a version grows (`1.10.0`), when a state changes (`upgrade to 9.9.9`) and when one arrives from
the dipòsit while the page is on the glass — the light update writes the words and never has to move a box (docs/30).

**El dibuix es fa en el moment de pintar i no en el de fer la fila**: una icona que encara no ha arribat es veu quan arriba.
]]
-- El to d'una fila: la tinta de les que no hi són, un gris mig per a les que ja hi són, i la tinta a la fila que té el fons
-- gris (una app més nova aquí que al dipòsit), que és l'única on el gris clar no es llegiria.
local function ink_of(row)
  if row.state == 'newer here' then return INK end
  if row.state == 'not here' then return INK end
  return kInstalled
end

-- **El tros que hi cap, partit en dues línies per l'app** (0.200.4): a label of this system is one line and cuts it with dots,
-- i el que vol una fitxa de tres pisos és dues línies senceres amb els punts només a l'última. La mesura es fa amb
-- `ui.text_width` mentre la pàgina es construeix, que és quan la cara és la que es dibuixa (docs/30).
local function two_lines(says, width, scale)
  local first, second = '', ''
  for word in tostring(says):gmatch('%S+') do
    local line = (second ~= '') and second or first
    local next_one = (line == '') and word or (line .. ' ' .. word)
    if line ~= '' and ui.text_width(next_one, scale) > width then
      -- **La primera línia és la que s'està omplint mentre `second` és buit** (0.200.4, i es va veure al vidre: la segona línia
      -- deia només `...`): `first` ja té paraules quan la primera línia es trenca, o sigui que la pregunta és si la segona encara
      -- no ha començat i no si la primera és buida.
      if second == '' then
        first = line
        second = word
      else
        second = second .. ' ...'
        return first, second
      end
    elseif second ~= '' then
      second = next_one
    else
      first = next_one
    end
  end
  return first, second
end

-- **Què diu una app, demanat una vegada** (0.200.4): the manifest of an app is already on the cache after a Refresh, and a row of
-- the list reads it the first time that app appears in a page and never again in this run —un `cached_manifest` per fila i per
-- pintada seria un fitxer i un descodificador per cada quatre files (docs/30, docs/70).
local blurbs = {}
local function blurb_of(id)
  local said = blurbs[id]
  if said ~= nil then return said end
  local data = store.cached_manifest(id)
  said = ''
  if data ~= nil and data.description ~= nil then
    said = store.plain(tostring(data.description)):gsub('%s+', ' ')
  end
  blurbs[id] = said
  return said
end

-- I les captures, que només existeixen a l'app que hi és (un retall de la primera, 0.200.4).
local function a_row(row, shown, at)
  local row_w = wid - 2 * Margin
  -- **Una fila que no es dona es queda pintada** (0.200.2, i es va veure al vidre: la llista sencera a sota de la fitxa): the
  -- rows live on the page and not inside a `ui.list` any more, so nothing of the system gives them back —`keep` puts them with
  -- the other controls of the page, and `build_rows` releases the ones it made before making the next ones (docs/30, docs/90).
  local entry = keep(ui.list_entry(row_w, kRow))
  if entry == nil then return nil end
  entry.background = (row.state == 'newer here') and kNewer or PAPER
  entry.on_click = function()
    chosen = row.id
    asked = 'detail'
    save_state()
  end
  shown.entry = entry

  local turned = (at % 2 == 0)
  local line_body = ui.line_height(kBody)
  local line_small = ui.line_height(kSmall)
  -- **Les paraules van enganxades a la icona i no a la vora de la fila** (0.200.0, l'amo: *«el títol de l'app imparell
  -- hauría d'estar enganxada a l'icona de la part dreta, com ja ho estàn a la esquerra»*): the box of the words is the same
  -- in both rows —from the icon's side to the row's other edge— and what turns is where it starts and which edge the words
  -- touch. A title that is flush left on a row whose icon is on the right is a title that belongs to no drawing.
  local floor_y = 8
  local words_x = turned and 8 or (8 + kIcon + 12)
  local words_w = row_w - kIcon - 28

  local picture = ui.canvas(kIcon, kIcon)
  if picture ~= nil then
    picture:place(turned and (row_w - 8 - kIcon) or 8, floor_y)
    picture.on_draw = function(self)
      shown.skin = icon_of(row.id)
      self:clear(entry.background)
      if shown.skin ~= nil then self:image(0, 0, shown.skin, kIcon, kIcon, nil, true) end
    end
    shown.picture = picture
    entry:add(picture)
  end

  -- **El títol al capdamunt de la columna de les paraules** (0.200.4): the icon is ninety-six tall and the title is not on the
  -- middle of it any more, perquè el que hi ha sota el títol és una frase i no aire.
  local at_y = floor_y
  local title = ui.label(words_w, line_body)
  if title ~= nil then
    title.text = row.title
    title.scale = kBody
    title.color = ink_of(row)
    title.align = turned and 'right' or 'left'
    title:place(words_x, at_y)
    entry:add(title)
    shown.title = title
  end

  -- **El que queda entre el títol i la ratlla de la versió** (0.200.4, l'amo: *«Hi ha una zona que queda buida es pot posar text
  -- entre el títol i el cartell de "newer here", queda molta tarjeta. Pots posar un trosset de l'inici de la descripció de l'app o
  -- si hi han captures de /res/shots/ uns retalls»*): dues línies del començament de la descripció dins la meitat de la fitxa que
  -- quedava en blanc. La icona i el títol són el pis de dalt; el que es llegeix d'una app és el segon (docs/30, docs/90).
  --
  -- **Les captures no hi són i no s'inventen** (i s'ha provat: `ui.canvas` per a un retall no s'arribava ni a fer en una fila amb
  -- la icona, i cap app del repositori no porta `res/shots/`). El dia que una app en porti, el lloc és aquest i el dibuix és un
  -- `cv:image` amb el retall centrat (docs/30, docs/70).
  do
    local band_y = floor_y + line_body + 4
    local band_h = kIcon - line_body - 4
    local first, second = two_lines(blurb_of(row.id), words_w, kTiny)
    if first ~= '' then
      local room = band_h
      for i, one in ipairs({ first, second }) do
        if one ~= '' and room >= ui.line_height(kTiny) then
          local says = ui.label(words_w, ui.line_height(kTiny))
          if says ~= nil then
            says.text = one
            says.scale = kTiny
            says.color = ink_of(row)
            says.align = turned and 'right' or 'left'
            says:place(words_x, band_y + (i - 1) * ui.line_height(kTiny))
            entry:add(says)
            if i == 1 then shown.blurb = says end
            room = room - ui.line_height(kTiny)
          end
        end
      end
    end
  end

  -- **La versió i el seu estat són un sol grup i no dues columnes** (0.200.0): the version touches the side of the icon and
  -- the state is stuck to it, a size smaller —`0.2.4  up to date`, and on the turned row `up to date  0.5.312`—, so the eye
  -- reads them together and the row's other end stays empty. Les dues caixes es tornen a col·locar quan el text canvia,
  -- perquè l'amplada d'un número no és sempre la mateixa (docs/30).
  local line_tiny = ui.line_height(kTiny)
  local under_y = floor_y + kIcon + 4
  local version = ui.label(row_w - 16, line_small)
  local state = ui.label(row_w - 16, line_tiny)
  if version ~= nil and state ~= nil then
    version.text = (row.version ~= '' and row.version) or '-'
    version.scale = kSmall
    version.color = ink_of(row)
    state.text = row.state
    state.scale = kTiny
    state.color = ink_of(row)
    entry:add(version)
    entry:add(state)
    shown.version = version
    shown.state = state
    -- **Cada etiqueta és tan ampla com el seu text** (0.200.2, l'amo: *«Clarament es el text que te fons blanc i tapa, perqué es
    -- tant llarg?»*): a label of this system paints its own paper, so a box of the whole row drawn after the version **erased it**
    -- on the turned rows —the ones whose words hug the icon on the right—, i el número de versió de cada fila de la dreta no hi
    -- era. La caixa es torna a mesurar quan el text canvia (`cv:resize`, que un label accepta perquè no té píxels propis) i el
    -- paper d'una no arriba mai al de l'altra (docs/30, docs/92).
    shown.place_under = function()
      local words = version.text or ''
      local says = state.text or ''
      local vw = math.floor(ui.text_width(words, kSmall)) + 6
      local sw = math.floor(ui.text_width(says, kTiny)) + 6
      pcall(function() version:resize(vw, line_small) end)
      pcall(function() state:resize(sw, line_tiny) end)
      local low = under_y + (line_small - line_tiny)     -- les dues línies comparteixen el peu
      if turned then
        version:place(row_w - 8 - vw, under_y)
        state:place(row_w - 8 - vw - 8 - sw, low)
      else
        version:place(8, under_y)
        state:place(8 + vw + 8, low)
      end
    end
    shown.place_under()
  end
  return entry
end

-- **El peu diu la finestra i el total** (0.200.2): `1-4 / 43 app(s)` és tot el que una llista que camina té per dir —on és la
-- persona i quantes n'hi ha— i `Slide for more!` surt només quan en queda alguna a sota. Els punts del paginador no hi són
-- perquè no hi ha pàgines (docs/30, docs/90).
local function say_the_count()
  local first = named.first or 1
  local last = named.last or 0
  local total = named.total or 0
  if total <= 0 then first = 0 end
  if named.counter ~= nil then
    named.counter.text = string.format('%d-%d / %d app(s)', first, last, total)
  end
  if named.more ~= nil then named.more.text = (last < total) and 'Slide for more!' or '' end
end

-- Les files que hi caben, a partir de la fila `first_`. Es torna a cridar quan el filtre canvia o quan el dit camina.
--[[
**LA LLISTA CAMINA AMUNT I AVALL I NO TÉ PÀGINES** (0.200.2, l'amo: *«Canviar el swipe dels costats per el d'amunt/avall dins
del control de la llista d'aplicacions del repositori. Sense el paginador es el mes natural»*): the list of the system turns a
page for a sideways finger and draws the strip of dots at its foot, i el que una botiga de quaranta-tres apps vol és caminar les
files; o sigui que l'app es queda la primera fila visible (`first_`), construeix només les que hi caben —cap fila es pot veure
tallada perquè cap no es fa fora de la caixa— i el dit que va amunt o avall la mou les files que ha caminat, llegit del gest de
`touch.gesture()` perquè un gest no arriba mai a l'arbre dels controls (docs/30, docs/90).
]]

local function build_rows()
  for _, shown in ipairs(rows_shown) do
    if shown.entry ~= nil then let_go(shown.entry) end
  end
  rows_shown = {}

  local rows = visible()
  local top, pager_y, per = list_geometry()
  local most = math.max(1, #rows - per + 1)
  if first_ > most then first_ = most end
  if first_ < 1 then first_ = 1 end

  -- **Les files que hi caben i prou**, filles de la pàgina i no d'una `ui.list` (0.200.2, l'amo: *«Canviar el swipe dels
  -- costats per el d'amunt/avall dins del control de la llista d'applicacions del repositori. Sense el paginador es el mes
  -- natural»*): the list of the system turns a page for a sideways finger and draws the strip of dots, and what a shop of
  -- forty-three apps wants is to walk the rows up and down, so the app keeps the first visible row (`first_`) and builds
  -- only the ones that fit —no row is ever clipped, because none of them is built outside the box— (docs/30, docs/90).
  for i = 0, per - 1 do
    local at = first_ + i
    local row = rows[at]
    if row == nil then break end
    local shown = { id = row.id }
    local entry = a_row(row, shown, at - 1)
    if entry ~= nil then
      entry:place(Margin, top + i * kRow)
      rows_shown[#rows_shown + 1] = shown
    end
  end

  since_build = 0
  named.first = first_
  named.last = math.min(#rows, first_ + per - 1)
  named.total = #rows
  say_the_count()
end

local function build_list()
  -- **El títol de la pàgina és el nom curt** (0.200.0): `Moonlight Installer` a la mida del títol fa cinc-cents tretze
  -- píxels i al costat del botó no hi cap, i un títol que s'acaba amb punts suspensius no és un títol. El nom sencer és al
  -- manifest i a la barra del sistema.
  local title = a_label('Moonlight', wid - 2 * Margin - kAskW - Gap, kTitle)
  if title ~= nil then title:place(Margin, Margin) end

  -- **Una fletxa circular i no una paraula dins d'un marc** (0.200.0, l'amo: *«El botó refresh es massa retro, una fletxa
  -- circular i deshabilitada per ara»*): the mark of the system called `reboot` is that arrow —a circle that starts again—, so
  -- the key is the drawing and nothing else (`framed` out), and while the shop does not have to ask the repository it is drawn
  -- in a mid grey and has no click at all: una porta que no fa res és pitjor que una porta que no hi és, i una marca apagada
  -- diu les dues coses de cop (docs/30, docs/90).
  local may = may_refresh() and not busy
  local refresh = keep(ui.button(kAskW, kAskH))
  if refresh ~= nil then
    refresh.icon = 'reboot'
    refresh.framed = false
    refresh.color = may and INK or kDim
    refresh.focusable = may
    if may then
      refresh.on_click = function()
        focus_ = 'refresh'
        asked = 'refresh'
        save_state()
      end
    end
    refresh:place(wid - Margin - kAskW, Margin)
    named.refresh = refresh
    named.may = may
  end

  -- **La caixa de cerca amb la seva lupa** (0.200.0): the magnifier is a canvas of its own to the left and the box takes
  -- the rest, because a text box of this system has no room for a drawing inside it (docs/30).
  local box_y = Margin + ui.line_height(kTitle) + Gap
  local lens_w = ui.line_height(kBody)
  local lens = a_magnifier(lens_w, ui.line_height(kBody) + 8)
  if lens ~= nil then
    lens:place(Margin, box_y)
    named.lens = lens
  end
  local box = keep(ui.textbox(wid - 2 * Margin - lens_w - Gap, ui.line_height(kBody) + 8))
  if box ~= nil then
    box.text = query
    box.cursor = #query
    box.scale = kBody
    box.on_change = function(self, text)
      query = tostring(text or '')
      first_ = 1
      focus_ = 'search'
      asked = 'filter'
      save_state()
    end
    box.on_focus = function(self, on)
      if on then
        focus_ = 'search'
        save_state()
      end
    end
    box:place(Margin + lens_w + Gap, box_y)
    named.search = box
  end

  build_rows()

  local _, pager_y = list_geometry()
  local counter = a_label('', wid - 2 * Margin - 200, kSmall)
  if counter ~= nil then
    counter:place(Margin, pager_y + 8)
    named.counter = counter
  end
  -- **«Slide for more!» i prou**: the two keys are gone (l'amo: *«Botons "next" i "back" son lo puto pitjor»*) perquè la
  -- llista del sistema passa la pàgina amb el dit i amb les fletxes del dial, i la invitació és una paraula i no un botó
  -- que no fa res quan no hi ha més pàgines (docs/30, docs/90).
  local more = a_label('', 200, kTiny)
  if more ~= nil then
    more.align = 'right'
    more:place(wid - Margin - 200, pager_y + 13)
    named.more = more
  end
  say_the_count()

  local line_y = hei - Margin - ui.line_height(kSmall)
  if progress_ ~= nil then
    -- **La barra baixa al peu i la línia puja** (0.200.2): the bar is the last thing on the glass, ten pixels tall, and what the
    -- app is saying moves up the fourteen it takes.
    line_y = line_y - 16
    local bar = keep(ui.progress(wid - 2 * Margin, 10))
    if bar ~= nil then
      bar:place(Margin, hei - Margin - 10)
      bar.value = math.max(-1, math.min(100, math.floor(progress_)))
      named.progress = bar
    end
  end
  local line = a_label('', wid - 2 * Margin, kSmall)
  if line ~= nil then
    line:place(Margin, line_y)
    named.status_line = line
    -- **Quan no hi ha res a dir, la línia diu quan es va preguntar el dipòsit**: una fletxa apagada sense cap paraula al
    -- costat és una fletxa que sembla espatllada (docs/30).
    line.text = (status_ ~= '') and status_ or asked_age()
  end
end

-- Una fila que ja és a la pàgina, posada al dia amb el que se sap ara: el nom, la versió i l'estat, el fons gris d'una
-- app més avançada aquí que al dipòsit, i la icona si n'ha arribat una de nova (docs/30).
local function update_one(shown, row)
  if shown.title ~= nil then shown.title.text = row.title end
  if shown.version ~= nil then shown.version.text = (row.version ~= '' and row.version) or '-' end
  if shown.state ~= nil then shown.state.text = row.state end
  if shown.place_under ~= nil then shown.place_under() end
  local ink = (row.state == 'newer here') and kNewer or PAPER
  if shown.entry ~= nil and shown.entry.background ~= ink then
    shown.entry.background = ink
    if shown.picture ~= nil then shown.picture:invalidate() end
  end
  -- El dibuix pregunta cada cop quin camí hi ha: si el de la icona ha canviat, es torna a descodificar, i si no, no.
  if shown.picture ~= nil and icon_of(row.id) ~= shown.skin then shown.picture:invalidate() end
end

-- **El que canvia quan el filtre canvia**: les files i la línia del comptador, i la caixa de text es queda on és amb el
-- cursor i el teclat (0.200.0, docs/30).
local function refilter()
  if not has_glass then return end
  build_rows()
  ui.present('gl16')
end

-- **El que canvia quan arriba una app del dipòsit**: el nom, la versió i el fons de les files que ja hi són, **tocant les
-- etiquetes** i sense refer cap fila —una fila és una icona, i quaranta-una icones descodificades quaranta-una vegades és
-- el que aquesta pàgina no fa (docs/30, docs/75). Si el que ha arribat no hi cabia (el filtre o la pàgina són uns altres),
-- es refan les files, però no a cada arribada: cada quatre, i sempre l'última.
local function update_rows()
  if not has_glass or page ~= 'list' then return end
  local rows = visible()
  -- **La finestra i no la llista sencera**: `rows_shown` són les files que hi ha al vidre, o sigui les que comencen a `first_`;
  -- comparar-les amb les primeres de la llista era comparar dues coses diferents (0.200.2).
  local same = (#rows - first_ + 1 == #rows_shown)
  if same then
    for i = 1, #rows_shown do
      if rows[first_ + i - 1].id ~= rows_shown[i].id then same = false break end
    end
  end
  if not same then
    if since_build < 4 and #queue > 0 then
      -- Les que hi ha s'actualitzen i les que falten entren quan toqui: la llista no batega a cada segon.
      for _, shown in ipairs(rows_shown) do
        local row = nil
        for _, one in ipairs(rows) do if one.id == shown.id then row = one break end end
        if row ~= nil then update_one(shown, row) end
      end
      ui.present('gl16')
      return
    end
    refilter()
    return
  end

  for i, shown in ipairs(rows_shown) do
    for _, row in ipairs(rows) do
      if row.id == shown.id then update_one(shown, row) break end
    end
  end
  named.first = first_
  named.last = first_ + #rows_shown - 1
  named.total = #rows
  say_the_count()
  ui.present('gl16')
end

-- ---------------------------------------------------------------- la pàgina: la fitxa

local function wrapped(says, width, scale)
  local lines = {}
  for piece in (says .. '\n'):gmatch('([^\n]*)\n') do
    local line = ''
    for word in piece:gmatch('%S+') do
      local next_one = (line == '') and word or (line .. ' ' .. word)
      if line ~= '' and ui.text_width(next_one, scale) > width then
        lines[#lines + 1] = line
        line = word
      else
        line = next_one
      end
    end
    lines[#lines + 1] = line
  end
  return lines
end

-- **La mida en paraules d'una persona** (0.200.3): the same three steps the file browser uses —bytes, kilobytes, megabytes—, or
-- the empty text when nobody knows, because a `0 B` next to an app that is here is a lie (docs/30, docs/45).
local function size_text(n)
  local bytes = tonumber(n) or 0
  if bytes <= 0 then return '' end
  if bytes < 1000 then return bytes .. ' B' end
  if bytes < 1000000 then return math.floor(bytes / 1024) .. ' KB' end
  return string.format('%.1f MB', bytes / 1048576)
end

-- **La data de publicació, en dia i no en rellotge**: a card says when the app was written and not the second it was, i el
-- `time.date` del sistema és qui la diu (el camp del minut és `min`, docs/30).
local function date_text(sec)
  local when = tonumber(sec) or 0
  if when <= 0 then return '' end
  local d = time.date(when)
  if d == nil or d.day == nil or d.month == nil or d.year == nil then return '' end
  return string.format('%02d/%02d/%04d', d.day, d.month, d.year)
end

local function build_detail()
  local id = chosen
  local here = have[id]
  -- **L'índex porta quatre camps i la fitxa en vol set** (0.200.0): the page showed `by ?` and `boards: any board` because
  -- the entry of the list has no author and no boards —the index is a line of four fields and nothing more—, i el manifest
  -- sencer és al cau des del Refresh. El que la llista sap mana (és el que la versió comparada ha dit) i el cau hi posa la
  -- resta (docs/70).
  local cached = store.cached_manifest(id) or {}
  local there = repo[id] or {}
  local data = {
    title = there.title or store.plain(cached.title or id),
    version = there.version or cached.version,
    author = store.plain(cached.author or ''),
    boards = cached.boards,
    says = store.plain(cached.description or ''),
  }

  local back = keep(ui.button(64, 48))
  if back ~= nil then
    back.icon = 'back'
    back.on_click = function()
      focus_ = 'back'
      asking = false
      asked = 'back'
      save_state()
    end
    back:place(Margin, Margin)
  end

  -- **I la porta de la galeria, a l'altra banda** (0.200.5, l'amo: *«Afegeix una altra botó-fletxa com la de "back" que tens a la
  -- vista detall per tornar, però cap a la dreta per obrir la galería de captures de pantalla»*): the same key of the system with the
  -- chevron turned round (the fifteenth mark, `forward`), at the top right where a page of this system puts the way on; a finger
  -- that goes right on the card is the same door (docs/30, docs/45).
  local on = keep(ui.button(64, 48))
  if on ~= nil then
    on.icon = 'forward'
    on.on_click = function()
      focus_ = 'forward'
      shot_ = 1
      asked = 'gallery'
      save_state()
    end
    on:place(wid - Margin - 64, Margin)
    named.forward = on
  end

  local y = Margin + 48 + Gap
  local skin = icon_of(id)
  local picture = nil
  if skin ~= nil then
    picture = keep(ui.canvas(kIconBig, kIconBig))
    if picture ~= nil then
      picture:place(Margin, y)
      picture.on_draw = function(self)
        self:clear(PAPER)
        self:image(0, 0, skin, kIconBig, kIconBig, nil, true)
      end
    end
  end

  local text_x = (picture ~= nil) and (Margin + kIconBig + Gap) or Margin
  local text_w = wid - text_x - Margin
  local title = a_label(tostring(data.title or id), text_w, kTitle)
  if title ~= nil then title:place(text_x, y) end
  local who = a_label(string.format('%s  -  by %s', id, tostring(data.author or '?')), text_w, kSmall)
  if who ~= nil then who:place(text_x, y + ui.line_height(kTitle) + 2) end
  y = y + math.max(kIconBig, ui.line_height(kTitle) + ui.line_height(kSmall) + 8) + Gap

  local mine = (here ~= nil) and (here.version or '') or ''
  local theirs = (data.version ~= nil) and tostring(data.version) or ''
  local boards = 'any board'
  if type(data.boards) == 'table' and #data.boards > 0 then boards = table.concat(data.boards, ', ') end
  -- **Quant ocupa i de quan és** (0.200.3, l'amo: *«falta el tamany del programa sencer (Mb/Kb/b) i la data de publicació (ctime
  -- del arxiu manifest.json)»*): the folder of the app walked and the date of its manifest, i el que no es pot saber és un guió
  -- i no un zero —una app que no hi és no té ni mida ni data, i una placa sense rellotge no en té de data (docs/30, docs/70).
  local mine_now = (here ~= nil) and store.footprint(id) or nil
  local mine_size = (mine_now ~= nil) and size_text(mine_now.bytes) or ''
  local mine_when = (mine_now ~= nil) and date_text(mine_now.mtime) or ''
  local facts = {
    { 'here', (mine ~= '') and mine or 'not installed' },
    { 'in the repository', (theirs ~= '') and theirs or '-' },
    { 'boards', boards },
    { 'size', (mine_size ~= '') and mine_size or '-' },
    { 'published', (mine_when ~= '') and mine_when or '-' },
  }
  for _, one in ipairs(facts) do
    local line = a_label(string.format('%s: %s', one[1], one[2]), wid - 2 * Margin, kSmall)
    if line ~= nil then
      line:place(Margin, y)
      y = y + ui.line_height(kSmall) + 2
    end
  end

  -- **La descripció sencera dins d'una caixa de només lectura** (0.200.1, l'amo: *«La vista detall, caldrà posar la
  -- descripció dins una caixa de text de només lectura»*): `ui.textbox_multiline` amb `readonly` i `wrap` —el text no es
  -- talla a la vora del vidre, es parteix sol— i sense vores, perquè el que s'ha de veure és la prosa i no un requadre dins
  -- d'una targeta que ja en té un. La caixa **rep el lloc que deixen les dades i el botó** (`floor`), o sigui que la fitxa
  -- d'una descripció de dues línies i la d'una de vint fan el mateix dibuix, i **el dit la mou** quan no hi cap: el gest el
  -- fa el sistema i n'hi ha prou amb registrar-lo perquè la caixa el prengui (docs/30, docs/90).
  -- **La clau de la fitxa és alta i no una tira** (0.200.1, l'amo: *«Fes mes alt el botó, així no es veurà la vora de l'icona
  -- sobre el botó»* i després *«una mica mes alt i ja no es veurà la vora de l'icona al botó»*): the mark of a key is a square of
  -- the **face's height** and the drawing keeps a fifth of it as air on every side, or sigui que una cara de seixanta-quatre
  -- deixa el dibuix tocant la vora del botó i el que es veu és el requadre de la icona i no la icona. Amb setanta-vuit la cara té
  -- deu píxels d'aire a dalt i a baix i el dibuix es llegeix sol (docs/30, docs/45).
  local action_h = ui.line_height(kBody) + 38
  -- **Mentre una feina és pel mig, la barra es queda el seu lloc sobre la clau** (0.200.2): the bar takes fourteen pixels off
  -- the description —which has its own way of scrolling— and the key below it is the one that cannot be pressed.
  local bar_h = (progress_ ~= nil) and 14 or 0
  local floor = hei - Margin - ui.line_height(kSmall) - Gap - action_h
  -- El que ve del dipòsit viu a `says` i el que es llegeix del manifest del cau porta el nom del camp del manifest.
  local says = tostring(data.says or data.description or '')
  if says ~= '' and floor - bar_h - y > ui.line_height(kSmall) then
    y = y + Gap
    local box = keep(ui.textbox_multiline(wid - 2 * Margin, floor - bar_h - y))
    if box ~= nil then
      box:place(Margin, y)
      box.text = says
      box.readonly = true
      box.framed = false
      box.wrap = true
      box.top = 0
      box.scale = kSmall
      box.color = INK
      box.background = PAPER
      box.on_swipe = function(self, rows) end
      named.says = box
      y = floor
    end
  end

  -- **Un sol botó i el que toca** (0.200.0): `Install` si no hi és, `Upgrade` si la del dipòsit és més nova, `Uninstall` si
  -- hi és. I la botiga no es desinstal·la a si mateixa (docs/30, docs/40).
  --
  -- **`Upgrade` vol dir pujar i no baixar** (0.200.0, i es va veure al vidre): the first run of the card of an app that is
  -- *newer here* offered `Upgrade` to the older version of the repository —`newer here` is not a reason to install an older
  -- one—, i el que hi ha per fer quan la d'aquí és més nova és treure-la (o res).
  local state = state_of(id)
  local newer_over_there = state:find('^upgrade to ') ~= nil
  local what = nil
  if here == nil then
    what = 'Install'
  elseif id == 'moonlight' then
    what = nil
  elseif newer_over_there then
    what = 'Upgrade'
  else
    what = 'Uninstall'
  end
  if what ~= nil then
    local action = a_button(what, wid - 2 * Margin, action_h, kBody, function()
      focus_ = 'action'
      if what == 'Uninstall' then
        -- **La clau de treure no treu res** (0.200.0): obre la targeta de la pregunta i el que hi ha a sota no es toca fins
        -- que el dit o el dial trien la porta.
        asking = true
        asked = 'detail'
        save_state()
        return
      end
      wanted = id
      asked = 'install'
      save_state()
    end, what ~= 'Uninstall')
    if action ~= nil then
      -- **Cada clau porta la seva marca i el seu to** (0.200.1, l'amo: *«Al botó Uninstall posa el color del botó en un to mes
      -- fosc, no el mateix color que els altres. El botó d'install que sigui mes claret i amb una icona de polze amunt»* i *«El
      -- botó Actualitzar, amb una icona d'actualitzar (no sé, fletxes amunt?)»*): the three meanings of the one key are three
      -- marks of the system —la paperera, el polze i els dos xevrons— and three greys of the sixteen of this glass, from the
      -- paper of `Install` to the tone of `Uninstall`, which is the one that takes something away and is not the one a finger
      -- should find first. The kernel keeps a square of the face's height at the side for the mark and centres the word in what
      -- is left, so the mark and the word are one key and the ink of both turns with the key (docs/30, docs/45).
      if what == 'Uninstall' then
        action.icon = 'trash'
        action.background = kToneOut
      elseif what == 'Install' then
        action.icon = 'thumb'
        action.background = kToneIn
      else
        action.icon = 'upgrade'
        action.background = kToneUp
      end
      action.icon_side = 'left'
      -- **La clau no es prem mentre la feina dura** (0.200.2): a control of this system has no `disabled`, so it is the ink and
      -- the click that go —`kDim` and no `on_click`—, which is the same pair the arrow of `Refresh` wears when it has nothing to
      -- ask (docs/30, docs/90).
      if busy then
        action.color = kDim
        action.on_click = nil
        action.focusable = false
      end
      action:place(Margin, floor)
      named.action = action
    end
  end

  if progress_ ~= nil then
    local bar = keep(ui.progress(wid - 2 * Margin, 10))
    if bar ~= nil then
      bar:place(Margin, floor - 12)
      bar.value = math.max(-1, math.min(100, math.floor(progress_)))
      named.progress = bar
    end
  end

  local line = a_label((status_ ~= '') and status_ or state, wid - 2 * Margin, kSmall)
  if line ~= nil then
    line:place(Margin, hei - Margin - ui.line_height(kSmall))
    named.status_line = line
  end

  -- **«Are you sure?» amb les portes amples** (0.200.0, l'amo: *«Espero que aquest botò tingui modal "Are you sure?" amb els
  -- botons ben amples»*): una targeta del sistema i no un dibuix de l'app —el marc de tres que diu que és a sobre, el missatge
  -- que s'ajusta en línies i no es talla, i dues portes que es reparteixen l'ample de la targeta (214 px cada una)—. El que diu
  -- és el que l'app perd de debò: `uninstall` treu els fitxers de l'app i deixa el que la persona va escriure (docs/30, docs/41).
  if asking then
    -- **La targeta no surt del vidre d'una placa petita**: `kAskCardH` is the height the question wants, and a glass shorter than
    -- that keeps the card inside the page and lets the message say fewer lines (docs/30).
    local ask_h = math.min(kAskCardH, hei - 2 * Margin)
    local card = keep(ui.dialog(wid - 2 * Margin, ask_h))
    if card ~= nil then
      card:place(Margin, math.max(Margin, (hei - ask_h) // 2))
      card.scale = kBody
      card.title = 'Are you sure?'
      card.text = string.format('%s leaves this board: its folder and its cache go, and the alarms it left in the '
                                .. 'queue. What you wrote in it stays.', tostring(data.title or id))
      card.note = 'it can be installed again'
      card.accept = 'Uninstall'
      card.cancel = 'Keep it'
      -- **El cancel·lar és la porta de defecte**, i no ho diu un senyal sinó dos: la vora de tres de la porta (0.185.3) i
      -- l'anell del dial, que hi comença. Treure una app no és el camí fàcil i la targeta no l'ha de triar ella (docs/90).
      card.emphasis = 1
      card.choice = 1
      card.on_accept = function(self)
        asking = false
        wanted = id
        asked = 'remove'
        save_state()
      end
      card.on_cancel = function(self)
        asking = false
        asked = 'detail'
        save_state()
      end
      named.asking = card
    end
  end
end

-- ---------------------------------------------------------------- la pàgina: la galeria

-- **Les captures es demanen una vegada per app i arrencada** (0.200.5): a list of the files of `res/shots/` does not change
-- under a page, i una galeria que les tornés a llistar a cada volta seria un `fs.list` per cada dit (docs/30, docs/70).
local gallery = {}
local function shots_of(id)
  local list = gallery[id]
  if list == nil then
    list = store.shots(id)
    gallery[id] = list
  end
  return list
end

-- **Una captura al vidre, tan gran com hi cap i sense estirar-la** (0.200.5): `ui.image` dibuixa dins la pàgina i no gasta cap
-- buffer de canvas —una captura és de cinc-centes per nou-centes i un canvas d'aquesta mida no hi és—, i el que sobra de la
-- caixa és aire a les dues bandes (docs/30, docs/75).
local function build_gallery()
  local id = chosen
  local cached = store.cached_manifest(id) or {}
  local there = repo[id] or {}
  local name = there.title or store.plain(cached.title or id)

  local back = keep(ui.button(64, 48))
  if back ~= nil then
    back.icon = 'back'
    back.on_click = function()
      focus_ = 'back'
      asked = 'detail'
      save_state()
    end
    back:place(Margin, Margin)
    named.back = back
  end
  local title = a_label(name .. '  -  pictures', wid - 2 * Margin - 64 - Gap, kSmall)
  if title ~= nil then
    title:place(Margin + 64 + Gap, Margin + (48 - ui.line_height(kSmall)) // 2)
    named.gallery_title = title
  end

  local list = shots_of(id)
  local top = Margin + 48 + Gap
  local foot = hei - Margin - ui.line_height(kSmall) - Gap
  if shot_ > #list then shot_ = #list end
  if shot_ < 1 then shot_ = 1 end
  local shot = list[shot_]
  if shot == nil then
    -- **Una galeria buida ho diu**: una pàgina en blanc és una pàgina que sembla espatllada (docs/30).
    local why = (have[id] ~= nil) and 'this app does not bring any picture' or 'this app is not on this board'
    local says = a_label(why, wid - 2 * Margin, kSmall)
    if says ~= nil then says:place(Margin, (top + foot - ui.line_height(kSmall)) // 2) end
  else
    local box_w, box_h = wid - 2 * Margin, foot - top
    local info = ui.image_info(shot)
    local w, h = box_w, box_h
    if info ~= nil and info.width > 0 and info.height > 0 then
      local scale = math.min(box_w / info.width, box_h / info.height)
      w = math.max(1, math.floor(info.width * scale))
      h = math.max(1, math.floor(info.height * scale))
    end
    ui.image(Margin + (box_w - w) // 2, top + (box_h - h) // 2, shot, w, h, nil, false)
  end

  local counter = a_label(string.format('%d / %d', (#list > 0) and shot_ or 0, #list), wid - 2 * Margin, kSmall)
  if counter ~= nil then
    counter:place(Margin, hei - Margin - ui.line_height(kSmall))
    named.counter = counter
  end
  if #list > 1 then
    local more = a_label('Slide for the next', 260, kTiny)
    if more ~= nil then
      more.align = 'right'
      more:place(wid - Margin - 260, hei - Margin - ui.line_height(kTiny) - 2)
      named.more = more
    end
  end
end

-- ---------------------------------------------------------------- la pàgina

local function paint()
  if not has_glass then return end
  give_back()
  ui.clear(PAPER)
  if page == 'gallery' and chosen ~= '' then
    build_gallery()
  elseif page == 'detail' and chosen ~= '' then
    build_detail()
  else
    page = 'list'
    build_list()
  end

  local target = nil
  if asking and named.asking ~= nil then
    -- **La targeta es queda el dial mentre és a sobre** (0.200.0): el cercle camina les dues portes i el centre prem la que hi
    -- ha, o sigui que la fitxa de sota no ha de rebre ni una volta (docs/30).
    target = named.asking
  elseif focus_ == 'search' then
    target = named.search
  elseif focus_ == 'refresh' then
    target = named.refresh
  elseif focus_ == 'back' then
    target = named.back
  elseif focus_ == 'next' then
    target = named.next
  elseif focus_ == 'action' then
    target = named.action
  end
  if target ~= nil then ui.focus(target) end

  ui.present((page ~= painted_) and 'gc16' or 'gl16')
  painted_ = page
  save_state()
end

function on_resize(w, h)
  wid, hei = w, h
  paint()
end

-- ---------------------------------------------------------------- el dipòsit, una app per torn

local function keep_index()
  local rows = {}
  for id, one in pairs(repo) do
    rows[#rows + 1] = { id = id, version = one.version, icon = one.icon, title = one.title }
  end
  store.save_index(rows)
end

-- **Una app del dipòsit, sencera**: el manifest (el nom, la versió i la icona) i la icona. L'índex es desa cada cop,
-- perquè una app que es tanca a mig Refresh no perdi el que ja tenia (docs/70).
local function fetch_one(id)
  local data, why = store.fetch_manifest(id)
  if data == nil then return false, why end
  -- **El text del manifest es passa per `plain`** (0.200.0): és UTF-8 i la cara és Latin-1, i una descripció amb un guió
  -- llarg sortia al vidre com tres lletres russes (docs/30, docs/45).
  repo[id] = {
    id = id,
    title = store.plain(data.title or id),
    version = tostring(data.version or ''),
    icon = tostring(data.icon or ''),
    author = store.plain(data.author or ''),
    boards = data.boards,
    says = store.plain(data.description or ''),
  }
  keep_index()
  store.fetch_icon(id, repo[id].icon)
  return true
end

local function refresh()
  the_status('asking the repository...')
  local ids, why = store.list_ids(say)
  if ids == nil then
    the_status('the repository did not answer: ' .. tostring(why))
    return false
  end
  repo = {}
  queue = {}
  for _, id in ipairs(ids) do queue[#queue + 1] = id end
  queue_total = #queue
  queue_done = 0
  progress_ = 0
  first_ = 1
  if page == 'list' then paint() end
  the_status(string.format('%d app(s) in the repository, asking one by one', #queue))
  return true
end

-- **Un torn, una app** (0.200.0): la cua la buida el bucle i no pas el Refresh, perquè una descàrrega és una espera i
-- una espera dins d'un ganxo no es pot fer (docs/30, docs/85). Mentre la cua té feina la línia de sota diu quant en falta.
local function step_queue()
  if #queue == 0 then return end
  local id = table.remove(queue, 1)
  local ok, why = fetch_one(id)
  if not ok then say(string.format('%s: %s', id, tostring(why))) end
  queue_done = queue_done + 1
  if queue_total > 0 then the_progress(queue_done * 100 / queue_total) end
  if #queue > 0 then
    the_status(string.format('%d left - %s', #queue, id), true)
  else
    -- **Quan s'acaba, la línia torna a dir l'edat del cau** (0.200.0): a long sentence about forty-one apps does not fit a
    -- line of this face and it was cut with dots; `asked just now` is shorter, is true, i és el que la fletxa apagada vol dir.
    say(string.format('all %d app(s) of the repository are in', repo_count()))
    status_ = ''
  end
  if #queue == 0 and page == 'list' then
    -- l'última: la pàgina es torna a fer perquè la fletxa torni a estar apagada dues hores més
    progress_ = nil
    paint()
  elseif page == 'list' then
    update_rows()
  else
    draw_now()
  end
end

-- ---------------------------------------------------------------- el que fa el botó de la fitxa

local function do_install(id)
  busy = true
  the_status('installing ' .. id .. '...')
  -- La barra i la clau morta s'han de veure **abans** que la feina comenci: el que ve després és una espera dins del mateix torn.
  progress_ = -1
  paint()
  local ok, why, files = store.install(id, function(what) the_status(tostring(what)) end)
  busy = false
  progress_ = nil
  installed_now()
  if ok then
    the_status(string.format('%s installed: %d file(s)', id, files or 0))
  else
    the_status(string.format('%s did not install: %s', id, tostring(why)))
  end
  paint()
end

local function do_remove(id)
  busy = true
  the_status('taking ' .. id .. ' out...')
  progress_ = -1
  paint()
  local ok, why, taken = store.uninstall(id, function(what) the_status(tostring(what)) end)
  busy = false
  progress_ = nil
  installed_now()
  if ok then
    -- **El que la porta s'ha endut, dit amb números**: files, alarmes i sessions són el que una persona vol saber quan una
    -- app no acaba de desaparèixer del tot (docs/30).
    local files = (type(taken) == 'table' and taken.files) or 0
    local alarms = (type(taken) == 'table' and taken.alarms) or 0
    the_status(string.format('%s is out: %d file(s), %d alarm(s)', id, files, alarms))
  else
    the_status(string.format('%s did not come out: %s', id, tostring(why)))
  end
  paint()
end

-- ---------------------------------------------------------------- la vida de l'app

load_state()
installed_now()
for _, former in ipairs(store.load_index()) do
  repo[former.id] = { id = former.id, title = former.title, version = former.version, icon = former.icon }
end
if (page == 'detail' or page == 'gallery') and chosen == '' then page = 'list' end
paint()

-- **I a l'arrencar es pregunta sol si el que se sap és vell** (0.200.0): no només el primer cop —una pàgina buida que espera un
-- botó és una pàgina que no diu què fer— sinó quan el cau té més de dues hores, que és el mateix termini que el botó. Amb el
-- cau calent la botiga s'obre sense tocar la ràdio (docs/30, docs/70).
if may_refresh() or next(repo) == nil then asked = 'refresh' end

function on_pause() save_state() end
function on_save() save_state() end
function on_close() save_state() end

-- **I la fletxa s'encén sola quan el cau fa dues hores** (0.200.0): the page is looked at every twenty seconds —a hundred turns
-- of the loop— and it is only rebuilt on the day the answer changes, which is once every two hours and not every second.
local since_check = 0

while true do
  sys.delay(200):await()
  since_check = since_check + 1

  -- **El dit amunt i avall camina les files** (0.200.2): a gesture never reaches the tree of the controls, so the page reads
  -- it here; the rows the finger has walked are the rows the window moves, and a gesture that is not taken here is a gesture
  -- that does nothing (docs/30, docs/90).
  local gesture = touch.gesture()
  while gesture do
    local kind = gesture.kind
    local dx, dy = gesture.dx or 0, gesture.dy or 0
    if kind == 'drag' and not asking and page == 'list' then
      if math.abs(dy) > math.abs(dx) and math.abs(dy) >= 60 then
        local steps = math.max(1, math.floor(math.abs(dy) / kRow))
        first_ = first_ + ((dy < 0) and steps or -steps)
        asked = 'scroll'
        save_state()
      end
    elseif kind == 'drag' and not asking and page == 'gallery' then
      -- **A la galeria el dit camina de costat**, que és com es passen les imatges a tot arreu (0.200.5).
      if math.abs(dx) > math.abs(dy) and math.abs(dx) >= 60 then
        shot_ = shot_ + ((dx < 0) and 1 or -1)
        asked = 'gallery'
        save_state()
      end
    elseif kind == 'drag' and not asking and page == 'detail' then
      -- **I el dit cap a la dreta de la fitxa és la porta de les captures** (0.200.5).
      if math.abs(dx) > math.abs(dy) and dx <= -60 then
        shot_ = 1
        asked = 'gallery'
        save_state()
      end
    end
    gesture = touch.gesture()
  end

  if asked == 'refresh' then
    asked = nil
    refresh()
  elseif asked == 'filter' then
    asked = nil
    refilter()
  elseif asked == 'page' or asked == 'scroll' then
    asked = nil
    paint()
  elseif asked == 'detail' then
    asked = nil
    page = 'detail'
    status_ = ''
    paint()
  elseif asked == 'gallery' then
    asked = nil
    page = 'gallery'
    paint()
  elseif asked == 'back' then
    asked = nil
    page = 'list'
    status_ = ''
    paint()
  elseif asked == 'install' then
    asked = nil
    do_install(wanted)
  elseif asked == 'remove' then
    asked = nil
    do_remove(wanted)
  end

  if not busy then step_queue() end

  if since_check >= 100 then
    since_check = 0
    if page == 'list' and named.may ~= nil and may_refresh() ~= named.may then paint() end
  end
end
