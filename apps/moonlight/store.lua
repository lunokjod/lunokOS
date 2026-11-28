--[[
Moonlight Installer :: store.lua — **el costat del dipòsit** (0.200.0).

The shop has two sides and this is the one that speaks to github.com: the list of apps of
`lunokjod/lunokOS/apps`, the `manifest.json` of each one, the icon each manifest names, and the files an install
downloads. **It repeats what the `repo` command does** —l'amo: *«Mira la comanda "repo", hem de fer les mateixes
funcions»*— with the doors an app has: `http.download` for the pages that are big and `http.get` for the small ones,
`fs` for its own ground, and the same order of steps: the listing is a download, every file of an app is a download,
and an app is in place only when the last of its files is (docs/30, docs/40, docs/70).

**Nothing here paints anything and nothing here awaits on its own**: every function that talks to the network is a
coroutine that the app's loop drives, and it says what it is doing through the `say` it is given, because a page that
takes a minute without a word is a page that looks dead (docs/30, docs/85).
]]

local json = require('json')

local M = {}

M.owner = 'lunokjod/lunokOS'
M.branch = 'main'
M.timeout = 20000

-- **Les tres carpetes del cau, totes dins del racó de l'app**: l'índex (una línia per app, el que la llista vol
-- sense obrir res), els manifests sencers (el que la pàgina de detalls vol) i les icones. Res del que es baixa es torna
-- a baixar mentre el cau hi sigui; el `Refresh` el buida i el torna a omplir.
function M.index_path() return fs.data('repo/index.txt') end
function M.list_path() return fs.data('repo/listing.json') end
function M.manifest_path(id) return fs.data('repo/m/' .. id .. '.json') end
function M.icon_path(id) return fs.data('repo/i/' .. id .. '.png') end
--[[
**ON VIUEN LES APPS DE DEBÒ** (0.200.0, i va caldre veure-ho al vidre): the `/apps` of the tree is a **door** and not a
folder —`vfs::resolve` answers a name of it with the disk that already has that name, the flash first and the card after—, so
a folder that does not exist yet is a name nobody answers: `http.download` into `/apps/.moonlight/…` settles with
`E_UNAVAIL`, which is exactly what the log said (`a file of the app did not come: banner.lua (E_UNAVAIL)`).

The shell's `repo install` writes on the volume itself (`/sd/apps/.<id>.install`) and takes the name with a rename, and the
shop does the same: the disk where the apps live is what `sys.settings().unit` says, and the one that has the folder is the
one the door would answer from (docs/30, docs/70).
]]
local function apps_root()
  local unit = ''
  local settings = sys.settings()
  if type(settings) == 'table' and type(settings.unit) == 'string' then unit = settings.unit end
  local order
  if unit == 'sd' then
    order = { '/sd/apps', '/littlefs/apps' }
  elseif unit == 'littlefs' then
    order = { '/littlefs/apps', '/sd/apps' }
  else
    order = { '/littlefs/apps', '/sd/apps' }
  end
  for _, one in ipairs(order) do
    if fs.stat(one) ~= nil then return one end
  end
  return '/apps'      -- una placa que no diu res es queda amb la porta i el que hi hagi
end

function M.apps_root() return apps_root() end
function M.work_path(id) return apps_root() .. '/.moonlight/' .. id end

-- El racó de l'app ja hi és (`fs.data()`), però les tres carpetes del cau no, i una escriptura en una carpeta que no hi
-- és no es fa (docs/70). Es demanen totes tres cada cop: repetir-ho val menys que endevinar quina falta.
function M.make_room()
  fs.mkdir(fs.data('repo'))
  fs.mkdir(fs.data('repo/m'))
  fs.mkdir(fs.data('repo/i'))
  return true
end

function M.list_url()
  return string.format('https://api.github.com/repos/%s/contents/apps?ref=%s', M.owner, M.branch)
end

local function raw_app(id)
  return string.format('https://raw.githubusercontent.com/%s/%s/apps/%s/', M.owner, M.branch, id)
end

local function api_app(id, inside)
  if inside == nil or inside == '' then
    return string.format('https://api.github.com/repos/%s/contents/apps/%s?ref=%s', M.owner, id, M.branch)
  end
  return string.format('https://api.github.com/repos/%s/contents/apps/%s/%s?ref=%s', M.owner, id, inside, M.branch)
end

-- ---------------------------------------------------------------- les versions

--[[
**DUES VERSIONS ES COMPAREN NÚMERO A NÚMERO I NO COM A CADENES** (0.200.0): `"1.10.0"` és més nova que `"1.9.0"` i una
comparació de textos diria el contrari. El que no és un número compta com un zero, i el que sobra mana si tot el que hi ha
abans és igual (`1.2` i `1.2.0` són la mateixa, i `1.2.1` és més nova que les dues).
]]
function M.compare(a, b)
  local left, right = tostring(a or ''), tostring(b or '')
  if left == right then return 0 end
  local i, j = 1, 1
  while i <= #left or j <= #right do
    local x = left:sub(i):match('^(%d*)')
    local y = right:sub(j):match('^(%d*)')
    local nx = tonumber(x) or 0
    local ny = tonumber(y) or 0
    if nx ~= ny then return (nx < ny) and -1 or 1 end
    if x == '' and y == '' then break end
    i = i + #x
    if i <= #left and left:sub(i, i) == '.' then i = i + 1 end
    j = j + #y
    if j <= #right and right:sub(j, j) == '.' then j = j + 1 end
    if x == '' and y == '' then break end
  end
  return 0
end

-- ---------------------------------------------------------------- el text del dipòsit

--[[
**UN MANIFEST ÉS UTF-8 I LA CARA DEL SISTEMA ÉS LATIN-1** (0.200.0, i es va veure al vidre): the card of `about` said
`Back â€”or a pinch, or three pressesâ€”` because the manifest's description is the JSON `\u2014` —an em dash in three
UTF-8 bytes— and the face draws bytes. So the store folds what it can back to Latin-1 (`C2`/`C3` plus one byte is one
Latin-1 byte: `·` is `C2 B7` in UTF-8 and `B7` on the glass), turns the punctuation a manifest really uses into ASCII,
and drops the rest instead of drawing three boxes (docs/30, docs/45).
]]
local kPunctuation = {
  ['\226\128\148'] = '--',  -- — em dash
  ['\226\128\147'] = '-',   -- – en dash
  ['\226\128\166'] = '...', -- … ellipsis
  ['\226\128\152'] = "'",   -- ‘
  ['\226\128\153'] = "'",   -- ’
  ['\226\128\156'] = '"',   -- “
  ['\226\128\157'] = '"',   -- ”
  ['\226\128\162'] = '*',   -- •
  ['\226\134\146'] = '->',  -- →
}

function M.plain(text)
  local said = tostring(text or '')
  for from, into in pairs(kPunctuation) do said = said:gsub(from, into) end
  said = said:gsub('\195([\128-\191])', function(c) return string.char(c:byte() + 64) end)
  said = said:gsub('\194([\128-\191])', function(c) return c end)
  said = said:gsub('[\224-\247][\128-\191][\128-\191][\128-\191]?', '')
  return said
end

-- ---------------------------------------------------------------- l'índex del cau

-- Una línia per app i un tabulador entre els quatre camps: `id`, `version`, `icon` i `title`. El títol es neteja de
-- tabuladors i de salts perquè una línia és una app (docs/70).
local function clean(text)
  local said = tostring(text or ''):gsub('[\t\r\n]', ' ')
  return said
end

function M.load_index()
  local rows = {}
  local text = fs.read(M.index_path())
  if type(text) ~= 'string' then return rows end
  for line in text:gmatch('[^\r\n]+') do
    local id, version, icon, title = line:match('^([^\t]+)\t([^\t]*)\t([^\t]*)\t(.*)$')
    if id ~= nil and id ~= '' then
      rows[#rows + 1] = { id = id, version = version or '', icon = icon or '', title = title or id }
    end
  end
  table.sort(rows, function(a, b) return string.lower(a.id) < string.lower(b.id) end)
  return rows
end

function M.save_index(rows)
  M.make_room()
  local lines = {}
  for _, one in ipairs(rows) do
    lines[#lines + 1] = table.concat({ one.id, clean(one.version), clean(one.icon), clean(one.title) }, '\t')
  end
  return fs.write(M.index_path(), table.concat(lines, '\n') .. '\n') ~= nil
end

-- **El manifest sencer també es desa**: la pàgina de detalls vol la descripció i les plaques, i tornar-lo a demanar per
-- ensenyar una fitxa seria una descàrrega per cada toc (docs/70).
function M.cached_manifest(id)
  local text = fs.read(M.manifest_path(id))
  if type(text) ~= 'string' then return nil end
  local data = json.decode(text)
  if type(data) ~= 'table' then return nil end
  return data
end

function M.forget_all()
  local folder = fs.list(fs.data('repo'))
  for _, one in ipairs(folder or {}) do
    local path = fs.data('repo/' .. one.name)
    if one.type == 'dir' then
      for _, inside in ipairs(fs.list(path) or {}) do fs.remove(path .. '/' .. inside.name) end
    end
    fs.remove(path)
  end
  return true
end

-- ---------------------------------------------------------------- el llistat

--[[
**LA LLISTA DE CARPETES, SENSE TENIR-LA A LA MEMÒRIA** (0.200.0): the answer of the contents API is thirty-one
kilobytes for forty apps, and a Lua string of thirty-one kilobytes next to the table of it is memory this board counts.
So the page goes to a file (`http.download` writes it as it arrives, docs/75) and the names are **scanned in pieces**: the
path of a folder is `"path": "apps/<id>"` —with the space of a pretty-printed page, which is how github answers— and a
name with a dot in it is a file and not an app.
]]
function M.list_ids(say)
  M.make_room()
  local path = M.list_path()
  fs.remove(path)
  local ok, answer = pcall(function() return http.download(M.list_url(), path, M.timeout):await() end)
  if not ok then return nil, tostring((type(answer) == 'table') and answer.code or answer) end
  local status = (type(answer) == 'table') and (answer.status or 0) or 0
  if status ~= 200 then return nil, string.format('HTTP %d', status) end
  local info = fs.stat(path)
  if info == nil or (info.size or 0) == 0 then return nil, 'the listing came empty' end

  local ids, seen = {}, {}
  local chunk = 2048
  local at, carry = 0, ''
  while at < info.size do
    local piece = fs.read_at(path, at, chunk)
    if type(piece) ~= 'string' or piece == '' then break end
    local text = carry .. piece
    for id in text:gmatch('"path"%s*:%s*"apps/([%w%-_%.]+)"') do
      if not id:find('%.') and not seen[id] then
        seen[id] = true
        ids[#ids + 1] = id
      end
    end
    carry = text:sub(-48)
    at = at + chunk
  end
  if say ~= nil then say(string.format('%d app(s) in the repository', #ids)) end
  return ids
end

-- ---------------------------------------------------------------- un manifest i una icona

-- **El manifest va al cau i es llegeix d'allà** (0.200.0): a page of two kilobytes would fit in the heap, but the file is
-- the cache and the cache is the file —one download, no copy, and the second visit to a shop that has been visited
-- before costs nothing (docs/70, docs/75).
function M.fetch_manifest(id)
  M.make_room()
  local into = M.manifest_path(id)
  fs.remove(into)
  local ok, answer = pcall(function() return http.download(raw_app(id) .. 'manifest.json', into, M.timeout):await() end)
  if not ok then return nil, tostring((type(answer) == 'table') and answer.code or answer) end
  local status = (type(answer) == 'table') and (answer.status or 0) or 0
  if status ~= 200 then fs.remove(into) return nil, string.format('HTTP %d', status) end
  local text = fs.read(into)
  if type(text) ~= 'string' then return nil, 'the manifest did not arrive' end
  local data = json.decode(text)
  if type(data) ~= 'table' then fs.remove(into) return nil, 'the manifest cannot be read' end
  if data.title == nil then fs.remove(into) return nil, 'the manifest has no title' end
  return data
end

function M.fetch_icon(id, icon)
  if icon == nil or icon == '' then return false end
  M.make_room()
  local into = M.icon_path(id)
  fs.remove(into)
  local ok, answer = pcall(function() return http.download(raw_app(id) .. icon, into, M.timeout):await() end)
  if not ok then return false end
  return (type(answer) == 'table') and ((answer.status or 0) == 200) and ((answer.bytes or 0) > 0)
end

-- ---------------------------------------------------------------- les carpetes d'una app

-- Una carpeta del dipòsit, amb el camí que li toca dins de l'app: el que la llista de continguts diu, que és el mateix
-- que el `repo install` camina (docs/30). **La resposta va a un fitxer i d'allà es llegeix**: una carpeta de vint fitxers
-- són cinc kilobytes i el que no es fa és reservar setze per si de cas (docs/75).
local function a_folder(id, inside)
  local into = fs.data('repo/ask.json')
  fs.remove(into)
  local ok, answer = pcall(function() return http.download(api_app(id, inside), into, M.timeout):await() end)
  if not ok then return nil, tostring((type(answer) == 'table') and answer.code or answer) end
  local status = (type(answer) == 'table') and (answer.status or 0) or 0
  if status ~= 200 then fs.remove(into) return nil, string.format('HTTP %d', status) end
  local entries = json.decode(fs.read(into) or '')
  if type(entries) ~= 'table' then return nil, 'that folder cannot be read' end
  local out = {}
  local prefix = 'apps/' .. id .. '/'
  for _, one in ipairs(entries) do
    if type(one) == 'table' and one.name ~= nil then
      local rel = tostring(one.path or '')
      if rel:sub(1, #prefix) == prefix then rel = rel:sub(#prefix + 1) end
      out[#out + 1] = {
        name = one.name,
        rel = rel,
        dir = (one.type == 'dir'),
        url = (one.download_url ~= nil) and tostring(one.download_url) or '',
        size = (type(one.size) == 'number') and one.size or 0,
      }
    end
  end
  return out
end

--[[
**UNA APP ARRIBA SENCERA O NO ARRIBA** (0.200.0, la mateixa regla que `repo install`): every file goes into a work folder
under `/apps/.moonlight/<id>/` —which the launcher sees as a folder with no manifest and so as nothing—, and only when the
last of them is in does it take the app's name: the old folder goes and the work folder is renamed. A chain that dies
halfway leaves the app of before, untouched, and its folder of the work is thrown away (docs/30, docs/70).
]]
function M.install(id, say)
  if id == nil or id == '' then return false, 'which app?' end
  if id == 'moonlight' then return false, 'the shop does not overwrite itself while it runs' end
  local root = apps_root()
  local work = root .. '/.moonlight/' .. id
  local ok, why = pcall(function()
    fs.mkdir(root .. '/.moonlight')
    M.drop(work)
    fs.mkdir(work)
  end)
  if not ok then return false, 'the disk did not make room: ' .. tostring(why) end

  local files, folders = 0, 0

  --[[
  **UN FITXER QUE HI ERA ES TREU ABANS DE BAIXAR-LO** (0.200.0, i l'amo ho va permetre: *«En cas de instalar o actualitzar
  tens permís per esborrar fitxers preexistents»*): a download that finds the name taken answers with a refusal and not with
  a page, and a folder of work left half done by an attempt of before is exactly a name taken —the first fitxer that arrived
  and the second one, which never did—. El que ha de quedar és el que arriba ara, i el que hi havia abans no val res.

  **I quan falla diu per què**: `HTTP 200, 0 B` i `HTTP 404, 0 B` són dues coses molt diferents i el registre de la placa
  només en tenia una, «a file of the app did not come», que no diu si és la xarxa, el nom o el disc (docs/30, docs/85).
  ]]
  local function save(url, into)
    if say ~= nil then say('...' .. into:match('([^/]+)$')) end
    fs.remove(into)
    local got, answer = pcall(function() return http.download(url, into, M.timeout):await() end)
    if not got then
      -- **Una promesa que es trenca porta una taula i no una frase**: `tostring` d'una taula és `table: 0x…`, que no diu
      -- res, i el que la porta hi posa és el codi (`E_PERM`, `E_NOVOLUME`) i el seu missatge (docs/30).
      if type(answer) == 'table' then return false, tostring(answer.code or answer.message or 'refused') end
      return false, tostring(answer)
    end
    local status = (type(answer) == 'table') and (answer.status or 0) or 0
    local bytes = (type(answer) == 'table') and (answer.bytes or 0) or 0
    if status ~= 200 then return false, string.format('HTTP %d', status) end
    if bytes == 0 then return false, 'the file came empty' end
    return true
  end

  -- **Les carpetes es caminen fins al fons** (0.200.0): cap app del dipòsit té avui més d'un nivell de carpeta, i una app
  -- que en tingués dos s'instal·laria sense els fitxers del segon i ningú no ho veuria fins que s'arrenqués (docs/30).
  local function bring(inside)
    local listing, why_here = a_folder(id, inside)
    if listing == nil then return false, tostring(why_here) end
    for _, one in ipairs(listing) do
      if one.dir then
        fs.mkdir(work .. '/' .. one.rel)
        folders = folders + 1
        local deeper, why_deeper = bring(one.rel)
        if not deeper then return false, why_deeper end
      elseif one.url ~= '' then
        local arrived, why_file = save(one.url, work .. '/' .. one.rel)
        if not arrived then
          return false, string.format('a file of the app did not come: %s (%s)', one.rel, tostring(why_file))
        end
        files = files + 1
      end
    end
    return true
  end

  local arrived, why_arrived = bring('')
  if not arrived then
    M.drop(work)
    return false, tostring(why_arrived)
  end

  if files == 0 then
    M.drop(work)
    return false, 'nothing came: is that an app of that repository?'
  end

  -- **I el manifest és el que diu que allò és una app**: a folder of files with no manifest is a folder nobody can
  -- start, and this is the last door before it takes the name of an app (docs/30, docs/40).
  if fs.read(work .. '/manifest.json') == nil then
    M.drop(work)
    return false, 'the app came with no manifest.json'
  end

  M.drop(root .. '/' .. id)
  local renamed, rename_why = fs.rename(work, root .. '/' .. id)
  if not renamed then
    M.drop(work)
    return false, 'the app could not be put in place: ' .. tostring(rename_why or 'the disk said no')
  end
  return true, nil, files, folders
end

-- **Una carpeta sencera fora**: els fitxers primer i després les carpetes, que una carpeta amb coses a dins no se'n va
-- (docs/70). El `drop` del nucli fa el mateix des de la 0.189.157, i aquí es fa amb el que una app té.
--
-- **I la llista de camins que es poden buidar és curta** (0.200.1): `drop` is the one function of this app that removes
-- files, and a path built from an empty name —`<root>/apps/` is the **door** and not a folder— would walk every app of the
-- board and empty them one by one (`/apps` answers nothing to `list` but the names a disk has, and each of those is a
-- folder). Només la carpeta de feina d'una app i la carpeta d'una app: `rest` is what is under the root of the apps.
local function droppable(path)
  if type(path) ~= 'string' then return false end
  local root = apps_root()
  if path:sub(1, #root + 1) ~= root .. '/' then return false end
  local rest = path:sub(#root + 2)
  return rest:match('^[%w%-_%.]+$') ~= nil or rest:match('^%.moonlight/[%w%-_%.]+$') ~= nil
end

local function empty(path)
  local entries = fs.list(path)
  if type(entries) ~= 'table' then return false end
  for _, one in ipairs(entries) do
    local into = path .. '/' .. one.name
    if one.type == 'dir' then
      empty(into)
    else
      fs.remove(into)
    end
  end
  return fs.remove(path) ~= nil
end

function M.drop(path)
  if not droppable(path) then return false end
  return empty(path)
end

--[[
**TREURE UNA APP ÉS FEINA DE LA PORTA DEL SISTEMA** (0.200.0): `admin.uninstall` s'endú la carpeta, les alarmes que l'app
havia deixat a la cua, la seva memòria cau de sessions i apuja la generació de la llista d'apps —que és com el llançador
sap que la seva graella és d'una app que ja no hi és—, i la botiga és una app d'administrador justament per a això. El que
no toca són `/data/<id>` i `/cfg/<id>`, que són de la persona (docs/30, docs/40, docs/70). El recorregut a mà es queda pel
cas d'una placa on la porta no hi sigui, i diu el mateix que diria ella.
]]
function M.uninstall(id, say)
  if id == nil or id == '' then return false, 'which app?' end
  if id == 'moonlight' then return false, 'the shop does not take itself out' end
  if fs.stat('/apps/' .. id) == nil then return false, 'that app is not here' end
  if say ~= nil then say('taking ' .. id .. ' out') end
  if admin ~= nil and admin.uninstall ~= nil then
    local taken, why = admin.uninstall(id)
    if taken == nil then return false, tostring((why ~= nil and (why.message or why)) or 'the system said no') end
    -- **El que se n'ha endut, dit**: files, bytes, alarmes i sessions són el que la porta torna i el que una persona vol
    -- saber quan una app no acaba de desaparèixer (docs/30).
    return true, nil, taken
  end
  if not M.drop(apps_root() .. '/' .. id) then return false, 'the disk would not let go of a file of it' end
  return true
end

-- **Quant ocupa una app i quan es va publicar** (0.200.3, l'amo: *«falta el tamany del programa sencer (Mb/Kb/b) i la data de
-- publicació (ctime del arxiu manifest.json)»*): the folder of the app of this board walked once —the size a person sees is the
-- one the disk is spending, and not what a manifest promises— and the `mtime` of its `manifest.json`, which is the file the date
-- comes from. **Un zero no és l'u de gener**: a memory that keeps no dates answers zero, and then there is no date to say
-- (docs/30, docs/70). Una app que no hi és no té mida ni data, i això és el que torna: `nil`.
-- **I la carpeta es busca on és i no on s'instal·la** (0.200.3, i es va veure a l'emulador: la fitxa deia `size: -`): `apps_root()`
-- answers the volume a **new** app goes to —the card first, then the flash— and on a board with a card that is not where the apps
-- are; a query that only reads has to ask the disk the app came from, que és el que la porta diu de cada nom (docs/30, docs/70).
local function the_folder_of(id)
  local order = { '/littlefs/apps', '/sd/apps' }
  if apps_root() == '/sd/apps' then order = { '/sd/apps', '/littlefs/apps' } end
  for _, root in ipairs(order) do
    local path = root .. '/' .. id
    if type(fs.list(path)) == 'table' then return path end
  end
  return apps_root() .. '/' .. id
end

-- **Les captures d'una app** (0.200.5, l'amo: *«obre la galería de captures de pantalla. (Cada app te la seva)»*): the pictures of
-- `res/shots/` of the app **on this board**, in name order, which is the only place where they can be read without a download.
-- Una app que no en porta no en té cap, i qui pregunta ho diu amb una taula buida (docs/30, docs/70).
function M.shots(id)
  local found = {}
  if id == nil or id == '' then return found end
  local folder = the_folder_of(id) .. '/res/shots'
  local entries = fs.list(folder)
  if type(entries) ~= 'table' then return found end
  local names = {}
  for _, one in ipairs(entries) do
    if one.type ~= 'dir' and one.name:match('%.png$') then names[#names + 1] = one.name end
  end
  table.sort(names)
  for _, name in ipairs(names) do found[#found + 1] = folder .. '/' .. name end
  return found
end

function M.footprint(id)
  if id == nil or id == '' then return nil end
  local root = the_folder_of(id)
  local bytes, files = 0, 0
  local function walk(path)
    local entries = fs.list(path)
    if type(entries) ~= 'table' then return end
    for _, one in ipairs(entries) do
      if one.type == 'dir' then
        walk(path .. '/' .. one.name)
      else
        bytes = bytes + (tonumber(one.size) or 0)
        files = files + 1
      end
    end
  end
  walk(root)
  if files == 0 then return nil end
  local info = fs.stat(root .. '/manifest.json')
  local when = (info ~= nil and tonumber(info.mtime)) or 0
  if when < 0 then when = 0 end
  return { bytes = bytes, files = files, mtime = when }
end

-- ---------------------------------------------------------------- qui hi ha i qui no

-- El que aquesta placa té muntat, amb el nom i la versió que el manifest diu: `apps.info` és la porta del sistema i cap
-- manifest no es llegeix dues vegades (docs/30).
function M.installed()
  local here = {}
  for _, id in ipairs(apps.list() or {}) do
    local info = apps.info(id)
    if info ~= nil then
      here[id] = { id = id, title = info.title or id, version = info.version or '',
                   icon = info.icon or '', works_here = info.works_here }
    end
  end
  return here
end

return M
