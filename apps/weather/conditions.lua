--[[
weather: **els codis del cel d'OpenWeatherMap, desats aquí** (0.199.8, l'amo: *«Guarda la llista de codis... vaig a buscar-te
icones»*, amb https://openweathermap.org/api/weather-conditions#Weather-Condition-Codes-2 a la mà).

La llista és la que publica el servei i es llegeix **sense xarxa**: cada `weather[].id` que pot arribar en una resposta, el seu
grup —el `main` que el servei hi posa—, les paraules que el servei mateix fa servir per a ell, i **la icona a què pertany**.

Les icones són **divuit** —nou dibuixos i la seva versió de nit— i són **a `res/` de l'app** amb el nom del codi: `01d.png`,
`01n.png`, `02d.png`, `02n.png`, `03d.png`, `03n.png`, `04d.png`, `04n.png`, `09d.png`, `09n.png`, `10d.png`, `10n.png`,
`11d.png`, `11n.png`, `13d.png`, `13n.png`, `50d.png` i `50n.png`. Són les del servei (100 × 100, amb alfa) i `file()` en dóna el
camí que `ui.image` vol. Res d'aquest fitxer no dibuixa res encara: diu què vol dir un número i a quin dibuix pertany, i prou.

**Un codi que la taula no coneix no és cap error**: la resposta sempre porta el `main`, la `description` i la `icon`, i l'app diu
els que hi ha. Aquesta taula és per poder dir el cel **sense** dependre del que el servei hagi escrit.

| Codi | Grup | Paraules del servei | Icona |
|---|---|---|---|
| 200–232 | Thunderstorm | thunderstorm, amb pluges i plugims de tota mena | 11d |
| 300–321 | Drizzle | drizzle, de fluixa a forta, i les seves barreges amb ruixats | 09d |
| 500–504 | Rain | light, moderate, heavy, very heavy i extreme rain | 10d |
| 511 | Rain | freezing rain | 13d |
| 520–531 | Rain | ruixats de pluja, de fluixos a esqueixats | 09d |
| 600–622 | Snow | neu, aiguaneu i les seves barreges amb pluja | 13d |
| 701–781 | Atmosphere | mist, smoke, haze, dust, fog, sand, ash, squalls i tornado | 50d |
| 800 | Clear | clear sky | 01d / 01n |
| 801–804 | Clouds | few, scattered, broken i overcast clouds | 02d / 03d / 04d |
]]

local M = {}

-- El codi, el grup, les paraules del servei i la icona del dia. La de la nit és la mateixa amb la `n` al darrere.
M.codes = {
  [200] = { 'Thunderstorm', 'thunderstorm with light rain', '11d' },
  [201] = { 'Thunderstorm', 'thunderstorm with rain', '11d' },
  [202] = { 'Thunderstorm', 'thunderstorm with heavy rain', '11d' },
  [210] = { 'Thunderstorm', 'light thunderstorm', '11d' },
  [211] = { 'Thunderstorm', 'thunderstorm', '11d' },
  [212] = { 'Thunderstorm', 'heavy thunderstorm', '11d' },
  [221] = { 'Thunderstorm', 'ragged thunderstorm', '11d' },
  [230] = { 'Thunderstorm', 'thunderstorm with light drizzle', '11d' },
  [231] = { 'Thunderstorm', 'thunderstorm with drizzle', '11d' },
  [232] = { 'Thunderstorm', 'thunderstorm with heavy drizzle', '11d' },

  [300] = { 'Drizzle', 'light intensity drizzle', '09d' },
  [301] = { 'Drizzle', 'drizzle', '09d' },
  [302] = { 'Drizzle', 'heavy intensity drizzle', '09d' },
  [310] = { 'Drizzle', 'light intensity drizzle rain', '09d' },
  [311] = { 'Drizzle', 'drizzle rain', '09d' },
  [312] = { 'Drizzle', 'heavy intensity drizzle rain', '09d' },
  [313] = { 'Drizzle', 'shower rain and drizzle', '09d' },
  [314] = { 'Drizzle', 'heavy shower rain and drizzle', '09d' },
  [321] = { 'Drizzle', 'shower drizzle', '09d' },

  [500] = { 'Rain', 'light rain', '10d' },
  [501] = { 'Rain', 'moderate rain', '10d' },
  [502] = { 'Rain', 'heavy intensity rain', '10d' },
  [503] = { 'Rain', 'very heavy rain', '10d' },
  [504] = { 'Rain', 'extreme rain', '10d' },
  [511] = { 'Rain', 'freezing rain', '13d' },
  [520] = { 'Rain', 'light intensity shower rain', '09d' },
  [521] = { 'Rain', 'shower rain', '09d' },
  [522] = { 'Rain', 'heavy intensity shower rain', '09d' },
  [531] = { 'Rain', 'ragged shower rain', '09d' },

  [600] = { 'Snow', 'light snow', '13d' },
  [601] = { 'Snow', 'snow', '13d' },
  [602] = { 'Snow', 'heavy snow', '13d' },
  [611] = { 'Snow', 'sleet', '13d' },
  [612] = { 'Snow', 'light shower sleet', '13d' },
  [613] = { 'Snow', 'shower sleet', '13d' },
  [615] = { 'Snow', 'light rain and snow', '13d' },
  [616] = { 'Snow', 'rain and snow', '13d' },
  [620] = { 'Snow', 'light shower snow', '13d' },
  [621] = { 'Snow', 'shower snow', '13d' },
  [622] = { 'Snow', 'heavy shower snow', '13d' },

  [701] = { 'Mist', 'mist', '50d' },
  [711] = { 'Smoke', 'smoke', '50d' },
  [721] = { 'Haze', 'haze', '50d' },
  [731] = { 'Dust', 'sand/dust whirls', '50d' },
  [741] = { 'Fog', 'fog', '50d' },
  [751] = { 'Sand', 'sand', '50d' },
  [761] = { 'Dust', 'dust', '50d' },
  [762] = { 'Ash', 'volcanic ash', '50d' },
  [771] = { 'Squall', 'squalls', '50d' },
  [781] = { 'Tornado', 'tornado', '50d' },

  [800] = { 'Clear', 'clear sky', '01d' },
  [801] = { 'Clouds', 'few clouds', '02d' },
  [802] = { 'Clouds', 'scattered clouds', '03d' },
  [803] = { 'Clouds', 'broken clouds', '04d' },
  [804] = { 'Clouds', 'overcast clouds', '04d' },
}

-- **Els divuit dibuixos i què volen dir**, amb el nom del fitxer que els toca a `res/` de l'app.
M.icons = {
  ['01d'] = 'clear sky', ['01n'] = 'clear sky',
  ['02d'] = 'few clouds', ['02n'] = 'few clouds',
  ['03d'] = 'scattered clouds', ['03n'] = 'scattered clouds',
  ['04d'] = 'broken clouds', ['04n'] = 'broken clouds',
  ['09d'] = 'shower rain', ['09n'] = 'shower rain',
  ['10d'] = 'rain', ['10n'] = 'rain',
  ['11d'] = 'thunderstorm', ['11n'] = 'thunderstorm',
  ['13d'] = 'snow', ['13n'] = 'snow',
  ['50d'] = 'mist', ['50n'] = 'mist',
}

-- El grup d'un codi que la taula no porta: la numeració del servei és per centenes i el 80x és el núvol.
local Group = { [2] = 'Thunderstorm', [3] = 'Drizzle', [5] = 'Rain', [6] = 'Snow', [7] = 'Atmosphere',
                [8] = 'Clouds' }

-- El dia o la nit d'una icona: `11d` a les onze del matí i `11n` a les onze de la nit.
function M.at_night(icon)
  if icon == nil or #icon < 3 then return icon end
  return icon:sub(1, 2) .. 'n'
end

function M.at_day(icon)
  if icon == nil or #icon < 3 then return icon end
  return icon:sub(1, 2) .. 'd'
end

-- El que se sap d'un codi: el grup, les paraules i la icona. `nil` només quan el codi no és ni un número.
function M.of(id)
  local code = tonumber(id)
  if code == nil then return nil end
  local known = M.codes[code]
  if known ~= nil then
    return { main = known[1], description = known[2], icon = known[3] }
  end
  local group = Group[math.floor(code / 100)]
  if group == nil then return nil end
  return { main = group, description = nil, icon = nil }
end

-- La icona d'un codi posada al dia o a la nit que la resposta diu (`sys.pod`, `d` o `n`).
function M.icon_of(id, pod)
  local what = M.of(id)
  if what == nil or what.icon == nil then return nil end
  if pod == 'n' then return M.at_night(what.icon) end
  return what.icon
end

-- **El camí del dibuix**: el `res/` d'una app és `/apps/<id>/res/` i el manifest és públic, o sigui que `ui.image` el pot
-- llegir (docs/30, docs/70). El dibuixos són de cent per cent i amb alfa: `ui.image` amb `mask` deixa el paper de sota.
M.folder = '/apps/weather/res/'

function M.file(icon)
  if icon == nil or icon == '' then return nil end
  return M.folder .. icon .. '.png'
end

return M
