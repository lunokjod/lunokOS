-- images: a picture on the whole glass, and the ones beside it (0.192.27).
--
-- **Aquesta app no navega pel disc** (l'amo: *«No facis un explorador dins de l'app visor d'imatges! ja tenim "Files" per això»*):
-- l'arbre és de `Files` i aquí només hi ha el vidre que una imatge es mereix. S'obre **amb un fitxer** —`open png /sd/foto.png`,
-- `run images /sd/foto.png` o una fila de `Files`— i el que fa és ensenyar-lo sencer, al més gran que mantingui la seva forma, i
-- **els costats del vidre passen a la del costat de la mateixa carpeta**, que és el que feia el visor del PaperOS obert amb un
-- camí (docs/45):
--
--   - un toc a la tercera part de l'esquerra o de la dreta, o un arròs de seixanta píxels cap a un costat, passa a l'anterior o
--     a la següent;
--   - el mig del vidre amaga i torna a ensenyar el peu, que és el que deixa la foto sola;
--   - i el camí de sortida és el del sistema: la creu de la targeta de la barra, el pessic de gran a petit i les tres tecles.
--
-- **Els formats són els del nucli i no una llista meva** (docs/30): `ui.image` llegeix PNG, JPEG, BMP, GIF (la primera imatge),
-- ICO, la família netpbm i el cau `ICS1` del sistema, i el que no llegeix ho diu amb el motiu a la pàgina en lloc de deixar-la
-- en blanc. Un format que arribi al sistema arriba aquí sol.

local caps = sys.caps()
if not caps.display then
  console.print('images: this board has no glass')
  return
end

local view = ui.caps()

-- A step of the face is eleven pixels of line with the bold face of the disk and seven with the table of the flash, so
-- the sizes are asked in pixels and the face answers with the steps it has (0.121.0, docs/30).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

local kSmall = scale_for(30)
local kMargin = 12
local kFootH = 2 * 30 + 10 -- the two lines of the bottom, which is the room the picture does not have
local kPictureMode = 'gc16' -- a photograph is sixteen tones and a clean: `du` is text and menus (docs/50)
local kTextMode = 'gld16'
local kSwipe = 60           -- el mateix número del visor del paper: menys que això és un dit que s'ha mogut

-- ---------------------------------------------------------------- the shape of the view (`state`, docs/30)

local kStateBytes = 512
local kLayout = 'v1'
local state_block = nil

local function the_state()
  if state_block == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state_block = block
  end
  return state_block
end

local function escape(text)
  return (tostring(text or ''):gsub('\\', '\\\\'):gsub('\n', '\\n'))
end

local function unescape(text)
  return (text:gsub('\\(.)', function(c) return (c == 'n') and '\n' or c end))
end

local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'folder', 'picture' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. escape(now[key]) end
  end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  -- **El bloc és més gran que el que s'hi escriu**: el que hi ha darrere és el zero amb què el nucli el va fer, i una línia
  -- amb zeros al darrere no és cap valor (docs/30).
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
      if key ~= nil then now[key] = unescape(value) end
    end
  end
  if first then return nil end
  return now
end

local saved = load_state() or {}
local shape = { folder = saved.folder or '', picture = saved.picture or '' }

-- ---------------------------------------------------------------- the pictures of a folder

local pictures = {}   -- les imatges de la carpeta, per nom: {name, path, format, w, h}
local at_index = 0    -- quina és al vidre (0 = cap)
local showing_the_foot = true

local function name_of(path)
  return path:match('([^/]+)$') or path
end

local function folder_of(path)
  local folder = path:match('^(.*)/[^/]+$') or '/'
  if folder == '' then folder = '/' end
  return folder
end

local function join(folder, name)
  if folder == '/' then return '/' .. name end
  return folder .. '/' .. name
end

-- **Un fitxer és una imatge si el sistema en sap dir el format**: that is the whole test, and it is why this app carries no
-- table of extensions that could fall behind the decoders (docs/30).
local function picture_of(path)
  local info = ui.image_info(path)
  if info == nil or info.width == nil or info.height == nil then return nil end

  return info
end

local function by_name(a, b)
  local left, right = a.name:lower(), b.name:lower()
  if left == right then return a.name < b.name end
  return left < right
end

-- **Les imatges de la carpeta, i prou** (docs/45): the folders are not read and the names that begin with a dot are not
-- listed —the `.cache.ics` of a thumbnail beside the picture it belongs to is not a picture a person asked for.
local function read_the_neighbours(folder)
  pictures = {}
  local entries = fs.list(folder) or {}
  for i = 1, #entries do
    local entry = entries[i]
    local name = entry.name
    if name ~= nil and name:sub(1, 1) ~= '.' and entry.type ~= 'dir' then
      local path = join(folder, name)
      local info = picture_of(path)
      if info ~= nil then
        pictures[#pictures + 1] = { name = name, path = path, format = info.format or '', w = info.width,
                                    h = info.height }
      end
    end
  end
  table.sort(pictures, by_name)
end

-- ---------------------------------------------------------------- the page

local show_picture, step_picture

local function say_the_page()
  -- **La pàgina es renta abans de posar-hi res** (docs/30, docs/50): the picture covers the glass and the ink of the page
  -- before it has to go away whole.
  ui.clear(15)
end

-- **Una frase en lloc d'una pàgina en blanc** quan l'app s'obre sense cap fitxer: this app does not walk the disk —`Files`
-- does that— so what it says is how to get here with one (docs/30, docs/40). Dues línies, perquè una de sola sobrepassa
-- l'amplada del vidre a la cara del sistema.
local function build_nothing()
  say_the_page()
  pictures = {}
  at_index = 0
  ui.title('Images')

  local scale = kSmall
  local first = 'Open a picture from Files,'
  local second = 'or run images <path>.'
  local step = ui.line_height(scale)
  local top = math.floor((view.height - 2 * step) / 2)
  local one = math.floor((view.width - ui.text_width(first, scale)) / 2)
  local two = math.floor((view.width - ui.text_width(second, scale)) / 2)
  ui.text(one, top, first, scale, 0, kTextMode)
  ui.text(two, top + step + 8, second, scale, 0, kTextMode)
  ui.present('gc16')
  console.print('images: no file, nothing to show')
end

-- **La imatge, sencera i escalada al que hi cap**: the box is the page above the line of the bottom and the picture keeps its
-- shape —a photograph stretched to the glass is a photograph nobody recognises—. **Un factor enter quan creix** (les icones
-- del paper) **i el més proper quan encongeix**: `ui.image` no interpola i no n'hi ha per a què amb setze tons (docs/30).
show_picture = function(path)
  if path == nil or path == '' then
    build_nothing()
    return
  end

  local folder = folder_of(path)
  if folder ~= shape.folder or #pictures == 0 then
    shape.folder = folder
    read_the_neighbours(folder)
  end

  at_index = 0
  for i = 1, #pictures do
    if pictures[i].path == path then at_index = i end
  end
  if at_index == 0 then
    -- **Un fitxer que la carpeta no té és un fitxer que no hi és**: the list of the neighbours is the folder's, and a path
    -- that came from outside has to be one of them or there is nothing to pass to (docs/30).
    local info = picture_of(path)
    if info == nil then
      console.print('images: ' .. path .. ' is not a picture this system reads')
      build_nothing()
      return
    end
    pictures[1] = { name = name_of(path), path = path, format = info.format or '', w = info.width, h = info.height }
    at_index = 1
  end

  local one = pictures[at_index]
  say_the_page()
  ui.title(one.name)

  local box_w = view.width - 2 * kMargin
  local box_h = view.height - kFootH - 2 * kMargin
  if box_h < 40 then box_h = 40 end
  local factor = math.min(box_w / one.w, box_h / one.h)
  local scale = math.floor(factor)
  if scale < 1 then scale = factor end
  local draw_w = math.max(1, math.floor(one.w * scale + 0.5))
  local draw_h = math.max(1, math.floor(one.h * scale + 0.5))

  local left = kMargin + math.max(0, math.floor((box_w - draw_w) / 2))
  local top = kMargin + math.max(0, math.floor((box_h - draw_h) / 2))

  local ok, why = ui.image(left, top, one.path, draw_w, draw_h, kPictureMode)
  if ok == nil then
    -- **El que el sistema ha dit es diu a la pàgina**: a picture that this build does not read is a sentence and not a blank
    -- page (an interlaced PNG, a progressive JPEG, a file that is not a picture at all) (docs/30).
    console.print('images: ' .. one.path .. ' did not come out: ' .. tostring(why))
    ui.text(kMargin, top, tostring(why), kSmall, 0, kTextMode)
  end

  -- **Dues línies al peu**: el format amb les dues mides i quin lloc fa la imatge a la carpeta, i sota el que el dit pot fer
  -- amb ella —un costat passa d'una a l'altra i el mig amaga el peu—, que és el que una foto sola no diu (docs/30).
  if showing_the_foot then
    local said = string.format('%s  %dx%d  ->  %dx%d    %d/%d', one.format, one.w, one.h, draw_w, draw_h, at_index,
                               #pictures)
    ui.text(kMargin, view.height - kFootH, said, kSmall, 0, kTextMode)
    local hint = 'sides: pass   middle: hide this'
    local hint_w = ui.text_width(hint, kSmall)
    ui.text(view.width - kMargin - hint_w, view.height - kFootH + ui.line_height(kSmall) + 4, hint, kSmall, 0, kTextMode)
  end

  ui.present(kPictureMode)

  shape.picture = one.name
  save_state(shape)
  console.print(string.format('images: open %s %s %dx%d at %dx%d, %d of %d', one.path, one.format, one.w, one.h, draw_w,
                              draw_h, at_index, #pictures))
end

-- **Els costats passen d'una imatge a l'altra**, i la que arriba a la fi torna a la primera: a folder of three pictures that
-- stops at the third is a folder a person has to leave and enter again (the paper's viewer wraps, docs/45).
step_picture = function(delta)
  if #pictures == 0 or at_index == 0 then return end

  local next_index = at_index + delta
  if next_index < 1 then next_index = #pictures end
  if next_index > #pictures then next_index = 1 end
  show_picture(pictures[next_index].path)
end

-- **Les tres portes del comiat**: the shape is saved here as well, because an app does not always see the end coming
-- (docs/30, docs/40).
function on_pause() save_state(shape) end
function on_save() save_state(shape) end
function on_close() save_state(shape) end

-- --------------------------------------------------------------------- the app

-- **Un camí que arriba per la porta s'obre** (0.65.0): `open png /sd/foto.png` i una fila de `Files` donen a aquesta app el
-- fitxer que li demanen, que és l'única manera d'entrar-hi (docs/30, docs/40).
local asked = (app.args ~= nil) and (app.args.path or '') or ''
if asked ~= '' then
  console.print('images: asked for ' .. asked)
  show_picture(asked)
elseif shape.picture ~= '' then
  -- **I tornant d'un son profund, la imatge que era al vidre**: the shape is the folder and the name, and a picture that is no
  -- longer there is the page that says how to open one (docs/30).
  show_picture(join(shape.folder, shape.picture))
else
  build_nothing()
end

-- The page is the app's: the finger and the keys are the system's controls, and what this loop does is keep the kernel turning
-- and answer the two gestures of a picture (docs/30, docs/90).
while true do
  ui.present()

  if at_index > 0 then
    local gesture = touch.gesture()
    while gesture do
      -- **Els costats de la imatge**: a tap on the left third is the one before and the right third the one after.
      if gesture.kind == 'tap' or gesture.kind == 'long' then
        if gesture.x < view.width / 3 then
          console.print('images: the left of the picture')
          step_picture(-1)
        elseif gesture.x > (2 * view.width) / 3 then
          console.print('images: the right of the picture')
          step_picture(1)
        else
          -- **El mig amaga el peu i el torna a ensenyar**: the picture alone is what a person asked for, and the numbers are
          -- there when they are wanted (docs/30).
          showing_the_foot = not showing_the_foot
          show_picture(pictures[at_index].path)
        end
      elseif gesture.kind == 'drag' and math.abs(gesture.dx) >= kSwipe and math.abs(gesture.dx) > math.abs(gesture.dy) then
        -- **I l'arròs cap a un costat també**, que és el gest que el visor del paper ja tenia: cap a l'esquerra la següent i
        -- cap a la dreta l'anterior, o sigui que el dit va cap on va la pàgina (docs/45).
        console.print(string.format('images: a swipe of %d px', gesture.dx))
        step_picture((gesture.dx < 0) and 1 or -1)
      end
      gesture = touch.gesture()
    end
  else
    local gesture = touch.gesture()
    while gesture do gesture = touch.gesture() end
  end

  sys.delay(50):await()
end
