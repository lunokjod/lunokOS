-- repository: **les apps que té el repositori, dites pel repositori mateix** (docs/30, docs/70).
--
--   run repository        baixa el llistat del directori `apps/` i llista les carpetes
--
-- La pàgina del navegador (`github.com/…/tree/main/apps`) és HTML i **no és el que vol una app**: el que
-- vol és la llista, i GitHub la dona en JSON a l'API de continguts, que és la versió crua d'aquell
-- directori. Una carpeta hi és un objecte amb `"type": "dir"`, i el nom és `"name"`.
--
-- **El llistat no cap al heap i no hi ha de cabre**: són uns 44 KB amb les URL de cada entrada, i el
-- `http.get` de la placa en porta 16 384 per defecte. Va amb `http.download` a un fitxer del terreny de
-- l'app i es llegeix **a trossos**, que és el que fa qualsevol cosa gran en aquest sistema.
--
-- L'adreça viu aquí i no en un argument, com al `page`: és el que fa servir una persona que canvia el
-- repositori, i el que fa que l'app sigui un exemple i no una eina.
local kApi = 'https://api.github.com/repos/lunokjod/lunokOS/contents/apps'
local kRepo = 'github.com/lunokjod/lunokOS'
-- El tros amb què es llegeix el llistat: vuit kilobytes són dues o tres entrades per tros i cap
-- pressió per a l'intèrpret d'una placa sense memòria extra (docs/75).
local kChunk = 8192

-- ---------------------------------------------------------------------------------------------------
-- El fitxer, a trossos
-- ---------------------------------------------------------------------------------------------------

-- **Els noms de les carpetes, sense el document sencer a la memòria**: es va llegint el JSON i cada objecte
-- que ja ha arribat sencer es mira; el tros que queda a mitges es guarda per al tros següent (docs/70).
-- Un objecte de la resposta és `{"name": "...", ..., "type": "dir", ...}` i no té cap clau dins d'una
-- cadena, o sigui que `%b{}` és el seu final i no una casualitat.
local function folders_of(path)
  local handle, err = fs.open(path, 'r')
  if handle == nil then return nil, tostring(err and err.message or 'cannot open the listing') end
  local folders = {}
  local others = 0
  local pending = ''
  while true do
    local chunk = handle:read(kChunk)
    if chunk == nil then break end
    local buffer = pending .. chunk
    local at = 1
    while true do
      local start, stop = buffer:find('%b{}', at)
      if start == nil then break end
      local object = buffer:sub(start, stop)
      local name = object:match('"name"%s*:%s*"([^"]*)"')
      local kind = object:match('"type"%s*:%s*"([^"]*)"')
      if name ~= nil then
        if kind == 'dir' then
          folders[#folders + 1] = name
        else
          others = others + 1
        end
      end
      at = stop + 1
    end
    -- El que queda després de l'últim objecte sencer és o un tros d'objecte o el final del document.
    pending = buffer:sub(at)
  end
  handle:close()
  return folders, others
end

-- Una columna per nom, amb el que cap a la consola: vint-i-tantes carpetes en una llista d'una línia
-- cada una és mitja pantalla o més, i el que es vol llegir és quantes n'hi ha i com es diuen (docs/30).
local function print_in_columns(names, width)
  local widest = 0
  for _, name in ipairs(names) do
    if #name > widest then widest = #name end
  end
  local step = widest + 2
  local per_row = math.max(1, math.floor(width / step))
  local row = {}
  for i, name in ipairs(names) do
    row[#row + 1] = name .. string.rep(' ', step - #name)
    if #row == per_row or i == #names then
      -- **Els parèntesis són de debò**: `gsub` torna dues coses (la cadena i quants trossos ha canviat) i
      -- `console.print` les imprimiria totes dues separades per un tabulador (vist a la consola, 4/11).
      local line = (table.concat(row):gsub('%s+$', ''))
      console.print(line)
      row = {}
    end
  end
end

-- ---------------------------------------------------------------------------------------------------
-- El programa
-- ---------------------------------------------------------------------------------------------------

local caps = sys.caps()
console.print('repository: asking ' .. kRepo .. ' for apps/')
if not caps.net_wifi or not caps.net_http then
  console.print('this board has no radio or no TLS: there is no repository to ask')
  return
end

-- **El lloc on va el llistat és el d'un fitxer de l'app**: el seu `/data/repository/`, que el sistema crea
-- si no hi és. Una baixada és una escriptura i segueix la mateixa regla que `fs.write` (docs/70).
local listing = fs.data('apps.json')
local ok, result = pcall(function() return http.download(kApi, listing):await() end)
if not ok then
  local code = tostring(result and result.code)
  console.print('no listing: ' .. code)
  if code == 'E_NONET' then
    console.print('the board has no network saved: `wifi add <ssid> <pass>` first')
  elseif code == 'E_TLS' or code == 'E_HTTP' then
    console.print('the request did not come back: ' .. tostring(result and result.message))
  elseif code == 'E_NOSPC' or code == 'E_IO' then
    console.print('there is no room for the listing on this board: ' .. listing)
  end
  return
end
if result.status ~= 200 then
  console.print(string.format('the server answered %d, and a listing is a 200', result.status))
  return
end

local folders, others = folders_of(listing)
if folders == nil then
  console.print('cannot read the listing: ' .. tostring(others))
  return
end
table.sort(folders)

console.print(string.format('%d apps in %s (%d bytes of JSON, %d other entries)', #folders, kRepo,
  result.bytes, others or 0))
print_in_columns(folders, term.width())
console.print('run <name> to start one, and `apps` lists the ones this board carries')
