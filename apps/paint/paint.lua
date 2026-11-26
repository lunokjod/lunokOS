-- paint: la pàgina on el dit dibuixa i el dibuix no surt fins que el dit s'ha aixecat (0.192.47).
--
-- **El que l'amo va demanar** (*«Fer un app de dibuix a má alçada: Mentre el dit sigui al vidre no dibuixar res, guardar tots els
-- moviments del dit i pendre mostres del dit a la major velocitat possible mentre l'usuari traça el dibuix … Un cop el dit s'hagi
-- aixecat (passats 2,3 segons) es fa el dibuixat»*). Un vidre d'e-ink no és un lienç que segueix un dit: cada tinta que s'hi posa
-- és una passada del panell, i una passada per cada mostra d'un dit que es mou és un vidre que va mitja passa per darrere de la mà.
-- O sigui que aquesta pàgina fa el contrari del que sembla: **mentre el dit hi és no es pinta res**, el que es fa és apuntar-ne el
-- camí, i quan el vidre s'ha quedat quiet **dos segons i mig** tot el que s'ha apuntat es pinta d'un sol cop (`gc16`).
--
--   - **Les mostres són les del xip i prou**. El panell d'aquesta placa (GT911) dona un marc cada deu mil·lisegons i el nucli el
--     pregunta al mateix ritme; aquest bucle pregunta a `touch.read()` cada dos mil·lisegons mentre el dit hi és, o sigui que **no
--     se'n perd cap** i les que es repeteixen (el mateix marc llegit dues vegades) es llencen perquè el punt sigui el mateix. Un
--     bucle que preguntés molt més de pressa no trauria ni una mostra més: el que mana és el xip i no el bucle (docs/30, docs/50).
--   - **El paper és un `ui.canvas`** i no la pàgina: el dibuix s'ha de poder desar (una captura dels seus píxels) i s'ha de poder
--     omplir amb el cubell, i les dues coses demanen un canvas amb buffer propi (docs/30, docs/90).
--   - **El fitxer més barat que aquest sistema té és un `ICS1`**: quatre bits per píxel, l'amplada sempre parell, i una capçalera
--     de vuit bytes —`ICS1`, l'amplada i l'alçada—. És exactament el que `cv:capture` dona, o sigui que desar un dibuix és escriure
--     una capçalera i copiar el bloc, i el fitxer es pot tornar a obrir amb `cv:image` com qualsevol foto. **I és el format de les
--     miniatures d'aquest sistema** (`.<nom>.cache.ics`), o sigui que el dibuix que algú desa aquí té la seva cara feta pel sistema
--     sense que aquesta pàgina faci res (docs/46, C11, docs/70).
--   - **L'extensió és `.ics` i la demana el manifest** (`"opens": "ics"`), i **no hi ha cap `Open` al menú** (l'amo: *«No posis
--     l'opció "Open", ja es fà desde l'app Files o la comanda open»*): la porta que obre un dibuix és la del sistema —una fila de
--     `Files`, `run paint <camí>` o `app.run('paint', true, nil, camí)`— i el camí arriba a `app.args.path` (docs/40, docs/45).
--   - **`Export` escriu un `PGM`** —`P5`, vuit bits per píxel— perquè un format conegut per aquest sistema és el que una altra
--     pàgina pot obrir: `lib/img` llegeix la família netpbm i no n'escriu cap, i un PGM és una capçalera de text i els mateixos
--     píxels en gris, o sigui que aquesta app el pot escriure sense cap codificador (docs/30, docs/50).
--   - **Les eines són les del sistema**: el pinzell i el cubell són dues marques de `tools/iconart.py` (`paint-brush` i
--     `fill-drip`), com la casa, la roda i la creu de la targeta de la barra, i la paleta és **els setze tons que el vidre té** —un
--     color i no setze és una mentida d'aquest panell— (docs/45, docs/92).

local caps = sys.caps()
if not caps.display then
  console.print('paint: this board has no glass')
  return
end

local view = ui.caps()

-- A step of the face is eleven pixels of line with the bold face of the disk and seven with the table of the flash, so the
-- sizes are asked in pixels and the face answers with the steps it has (0.121.0, docs/30).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end
  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end
  return scale
end

local kBody = scale_for(34)
local kSmall = scale_for(24)
local kMargin = 12
local kBarRow = ui.line_height(kBody) + 12  -- the row of the bar of menus, which is also the room the paper does not have
local kBarHeight = ui.line_height(kBody) + 8
local kToolSide = 56                        -- the two keys of the tools, square
local kToolGap = 10
local kPaletteH = 28
local kNoteH = 22                            -- the line of the foot, where the page says what it has just done

-- **La quietud que vol dir «la mà ha marxat»** (l'amo: *«passats 2,3 segons»*): two and a half seconds, and it is the number of
-- this page and not of the system —a person who lifts a finger and puts it down again inside it is still drawing the same thing—.
local kIdleMs = 2500
local kPaperMode = 'gc16'  -- a drawing is a picture and not text: every pass of it is clean, not cheap
local kCheapMode = 'du'
local kStep = 2            -- the turn of the loop while a finger is on the glass: the chip reports every ten (docs/50)
local kQuiet = 40          -- and the turn when nobody is drawing, which is the one that costs nothing
local kStrokeCap = 4000    -- the points of one stroke: a digit that stays on the glass a minute must not fill the heap

-- **Els dos estris** (0.192.47): the brush paints a stroke of the chosen thickness and the bucket floods the region of the tone the
-- finger landed on. They are the two things a drawing needs and they are two keys because the finger of this board is one.
local kBrush = 1
local kBucket = 2
local kToolNames = { 'brush', 'bucket' }

-- ---------------------------------------------------------------- the file of a drawing (docs/46, C11)

local kExtension = 'ics'
local kHeader = 'ICS1'
local kHeaderBytes = 8
local kChunk = 4096        -- the bytes of a save that are read from the block at a time

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

local now = { tool = kBrush, size = 3, level = 0, file = '' }

local function save_state()
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'tool', 'size', 'level' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. escape(now[key]) end
  end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

local function read_state()
  local block = the_state()
  if block == nil then return end
  local text = block:read(0, kStateBytes)
  if text == nil or text == '' then return end
  local first = true
  for line in text:gmatch('[^\n]+') do
    if first then
      first = false
      if line ~= kLayout then return end  -- a shape of another version is thrown away and not migrated (docs/30)
    else
      local key, value = line:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = unescape(value) end
    end
  end
  -- **El que s'ha llegit es valora abans de fer-lo servir** (docs/30): a tool that does not exist and a level outside the glass are a
  -- page with no brush and a palette with no tone.
  now.tool = tonumber(now.tool) or kBrush
  if now.tool ~= kBrush and now.tool ~= kBucket then now.tool = kBrush end
  now.size = math.max(1, math.min(16, math.floor(tonumber(now.size) or 3)))
  now.level = math.max(0, math.min(15, math.floor(tonumber(now.level) or 0)))
  now.file = ''
end

read_state()

-- ---------------------------------------------------------------- the page

local page_w, page_h = view.width, view.height
local paper_w, paper_h = 0, 0
local paper_x, paper_y = kMargin, 0
local palette_x, palette_y = kMargin, 0
local palette_w = 0

local paper = nil       -- the canvas the strokes go into, which is what is saved
local palette = nil     -- the sixteen tones, in a canvas of their own so the hole of a menu gives it back
local brush_key = nil
local bucket_key = nil
local size_key = nil
local note_label = nil
local bar = nil

local strokes = {}      -- the ones the finger has drawn and nobody has painted yet
local drawing = false   -- a finger that started on the paper
local last_move = 0     -- the millisecond of the last sample of that finger
local picked = false    -- the palette has already taken the finger that is on it
local quitting = false
local note = ''         -- the last thing the page has to say, in the small face under the paper

-- ---------------------------------------------------------------- the pixels of the paper (a session block, docs/75)

local pixels = nil
local pixels_bytes = 0

local function the_pixels()
  local bytes = (paper_w // 2) * paper_h
  if bytes <= 0 then return nil end
  if pixels ~= nil and pixels:alive() then
    if pixels_bytes >= bytes then return pixels end
    -- **Un canvas més gran demana un bloc més gran**: the block grows and keeps what it had, which is the page as it was (docs/75).
    local bigger = sys.session('pixels', bytes)
    if bigger ~= nil then
      pixels = bigger
      pixels_bytes = bytes
      return pixels
    end
    return nil
  end
  local block = sys.session('pixels', bytes)
  if block == nil then return nil end
  pixels = block
  pixels_bytes = bytes
  return block
end

local function drop_the_pixels()
  if pixels == nil then return end
  sys.session_destroy('pixels')
  pixels = nil
  pixels_bytes = 0
end

-- ---------------------------------------------------------------- the palette, which is the sixteen tones of the glass

local function level_of_a_swatch(x)
  if palette_w <= 0 then return nil end
  local cell = palette_w / 16
  local index = math.floor((x - palette_x) / cell)
  if index < 0 then index = 0 end
  if index > 15 then index = 15 end
  return index
end

local function paint_the_palette(canvas)
  local cell = palette_w / 16
  for level = 0, 15 do
    local x = math.floor(level * cell)
    local w = math.floor((level + 1) * cell) - x
    if w < 2 then w = 2 end
    -- **El to i prou**: a swatch is the tone itself and a hairline of the middle gray round it, so the two ends of the palette—the ink
    -- and the paper— are still two boxes and not a hole and a hole (docs/30, docs/92).
    canvas:rect(x, 3, w - 2, kPaletteH - 6, level, kCheapMode)
    if level == now.level then
      for step = 0, 2 do
        canvas:frame(x - 1 - step, 2 - step, w - 2 + 2 * step, kPaletteH - 4 + 2 * step, 0, kCheapMode)
      end
    else
      canvas:frame(x, 3, w - 2, kPaletteH - 6, 8, kCheapMode)
    end
  end
end

-- ---------------------------------------------------------------- the strokes, which are points until the hand goes

local function start_a_stroke(x, y)
  if #strokes > 0 and strokes[#strokes].open then return end
  local points = { { x, y } }
  strokes[#strokes + 1] = { level = now.level, size = now.size, tool = now.tool, open = true, points = points }
end

local function add_a_point(x, y)
  local stroke = strokes[#strokes]
  if stroke == nil then return end
  local points = stroke.points
  local last = points[#points]
  if last ~= nil and math.abs(last[1] - x) < 1 and math.abs(last[2] - y) < 1 then return end
  if #points >= kStrokeCap then return end
  points[#points + 1] = { x, y }
end

-- **Un traç gruixut és una fila de rodones** (docs/30): the canvas has a line of one pixel and a disc of a radius, and the line of a
-- pen of four is the discs the pen leaves when it walks —the step is the radius, so two of them always touch and no bead shows—.
local function paint_a_stroke(stroke, canvas)
  local radius = math.floor(stroke.size / 2)
  local points = stroke.points
  if #points == 1 then
    points[2] = { points[1][1], points[1][2] }
  end
  for i = 2, #points do
    local ax, ay = points[i - 1][1], points[i - 1][2]
    local bx, by = points[i][1], points[i][2]
    if radius < 1 then
      canvas:line(ax, ay, bx, by, stroke.level, kPaperMode)
    else
      local dx, dy = bx - ax, by - ay
      local distance = math.sqrt(dx * dx + dy * dy)
      local step = math.max(1, radius)
      local n = math.max(1, math.ceil(distance / step))
      for k = 0, n - 1 do
        local t = k / n
        canvas:disc(math.floor(ax + dx * t + 0.5), math.floor(ay + dy * t + 0.5), radius, stroke.level, kPaperMode)
      end
    end
  end
  -- the last one, so a stroke that stops does not lose its tip
  local tip = points[#points]
  if radius < 1 then
    canvas:pixel(tip[1], tip[2], stroke.level, kPaperMode)
  else
    canvas:disc(tip[1], tip[2], radius, stroke.level, kPaperMode)
  end
end

-- **El dibuixat, d'un sol cop** (l'amo: *«Un cop el dit s'hagi aixecat … es fa el dibuixat»*): everything the fingers left is painted
-- and the glass is presented once, which is the whole difference between this page and a canvas that follows a hand (docs/30, docs/50).
local function paint_the_strokes()
  if paper == nil or #strokes == 0 then return end
  for _, stroke in ipairs(strokes) do
    stroke.open = false
    if stroke.tool == kBucket then
      local p = stroke.points[1]
      paper:fill(p[1], p[2], stroke.level, kPaperMode)
    else
      paint_a_stroke(stroke, paper)
    end
  end
  strokes = {}
  paper:invalidate()
  ui.present(kPaperMode)
end

-- ---------------------------------------------------------------- desar, obrir i exportar (docs/46, C11, docs/70)

local function the_folder()
  local base = fs.data()
  if base == nil then return nil end
  fs.mkdir(base)
  local folder = base .. '/drawings'
  fs.mkdir(folder)
  return folder
end

local function the_name_only(path)
  return (path or ''):match('([^/]+)$') or ''
end

local function with_another_extension(path, wanted)
  local stem = (path or ''):gsub('%.[%w]+$', '')
  return stem .. '.' .. wanted
end

local function a_free_name(folder)
  for i = 1, 999 do
    local name = string.format('%s/draw-%d.%s', folder, i, kExtension)
    if fs.stat(name) == nil then return name end
  end
  return string.format('%s/draw.%s', folder, kExtension)
end

-- **La capçalera d'un `ICS1`**: the four letters and the two numbers, the width made even because two pixels are a byte, and this is
-- the same arithmetic `img::ics_header` does on the other side of the system (docs/46, C11).
local function the_header()
  local wide = paper_w + (paper_w % 2)
  return kHeader .. string.char(wide % 256, wide // 256, paper_h % 256, paper_h // 256)
end

local function save_the_paper(path)
  if paper == nil then return nil, 'there is no paper to save' end
  local block = the_pixels()
  if block == nil then return nil, 'no room for the pixels of the drawing' end
  if paper:capture(block, 0) == nil then return nil, 'the pixels did not fit in the block' end
  local file = fs.open(path, 'w')
  if file == nil then return nil, 'the file would not open for writing' end
  file:write(the_header())
  local stride = paper_w // 2
  local total = stride * paper_h
  local at = 0
  while at < total do
    local want = math.min(kChunk, total - at)
    local chunk = block:read(at, want)
    if chunk == nil or #chunk == 0 then break end
    file:write(chunk)
    at = at + #chunk
  end
  file:close()
  now.file = path
  -- **El bloc dels píxels és un esborrany i es torna** (0.192.47): a session block of a hundred and eighty kilobytes is parked to the
  -- card on every pause —half a second of writing for a scratch— and what it holds is the drawing that is already in the file it was
  -- just written to. Asked again when it is needed, which is a capture and not a read (docs/75).
  drop_the_pixels()
  return path, nil
end

-- **`Export` i no `Save`**: un PGM de vuit bits és el que una altra pàgina d'aquest sistema obre (`lib/img` llegeix la família
-- netpbm), i el que aquesta app escriu és una capçalera de text i els mateixos píxels en gris —el nivell del vidre vegades disset,
-- que és el que la pantalla li ha ensenyat a un ull— (docs/30, docs/50).
local function export_the_paper(path)
  if paper == nil then return nil, 'there is no paper to export' end
  local block = the_pixels()
  if block == nil then return nil, 'no room for the pixels of the drawing' end
  if paper:capture(block, 0) == nil then return nil, 'the pixels did not fit in the block' end
  local file = fs.open(path, 'w')
  if file == nil then return nil, 'the file would not open for writing' end
  file:write(string.format('P5\n%d %d\n255\n', paper_w, paper_h))
  local stride = paper_w // 2
  for y = 0, paper_h - 1 do
    local row = block:read(y * stride, stride)
    if row == nil then break end
    local out = {}
    for i = 1, stride do
      local byte = row:byte(i) or 255
      out[#out + 1] = string.char((byte >> 4) * 17, (byte & 0x0F) * 17)
    end
    file:write(table.concat(out))
  end
  file:close()
  drop_the_pixels()
  return path, nil
end

local function open_the_drawing(path)
  if paper == nil then return nil, 'there is no paper' end
  paper:clear(15, kPaperMode)
  local ok, why = paper:image(0, 0, path, paper_w, paper_h, kPaperMode)
  if ok == nil then return nil, tostring(why or 'the picture did not open') end
  now.file = path
  save_state()
  paper:invalidate()
  ui.present(kPaperMode)
  return path, nil
end

local say_the_note  -- the line of the foot, which is said at the end of this page's doors

-- **El dibuix no es perd quan la pàgina marxa** (docs/30, docs/75): it is a document and not the shape of a view, so it goes to the
-- file it came from —or to a draft, when nobody has named it yet— and the next start opens it again. **I no es compta amb
-- `on_close`**: an app that ends by coming back from its own script —the `Close` of the File menu does exactly that— does not hear the
-- three doors of the shell (measured on the emulator, 0.192.47), o sigui que el camí que marxa diu-ho aquí abans de sortir (docs/40).
local function keep_the_drawing()
  if paper == nil then return end
  local ground = the_folder()
  if ground == nil then return end
  local path = (now.file ~= '') and now.file or (ground .. '/draft.' .. kExtension)
  local what, why = save_the_paper(path)
  if what == nil then say_the_note('the drawing was not kept: ' .. tostring(why or '?')) end
end

-- **La pàgina diu el que ha fet** (docs/30): a drawing that is saved with no word back is a page that nobody knows has saved it, i el
-- peu és una línia de la cara petita com la de l'editor d'aquest sistema.
function say_the_note(text)
  note = tostring(text or '')
  if note_label ~= nil then note_label.text = note end
  console.print('paint: ' .. note)
  if note_label ~= nil then ui.present(kCheapMode) end
end

-- **No hi ha cap targeta que demani un camí** (0.192.47, l'amo: *«No posis l'opció "Open", ja es fà desde l'app Files o la comanda
-- open»*): the page is a drawing and three doors —save it, export it, leave it— i el dibuix que s'obre ve del sistema. El que hi havia
-- aquí era el full d'un camí amb el seu teclat, i sense `Open` al menú no hi ha res que el demani (docs/30, docs/40).

-- ---------------------------------------------------------------- the page and the tools

local function the_paper_room()
  local tools_y = kBarRow + 6
  local palette_y_here = tools_y + kToolSide + 8
  local paper_top = palette_y_here + kPaletteH + 8
  local paper_bottom = page_h - kMargin - kNoteH
  local wide = page_w - 2 * kMargin
  if wide % 2 ~= 0 then wide = wide - 1 end
  local tall = paper_bottom - paper_top
  if tall < 120 then tall = 120 end
  return wide, tall, paper_top, palette_y_here
end

local function create_the_page()
  page_w, page_h = ui.caps().width, ui.caps().height
  paper_w, paper_h, paper_y, palette_y = the_paper_room()
  paper_x = kMargin
  palette_x = kMargin
  palette_w = page_w - 2 * kMargin
  if palette_w % 2 ~= 0 then palette_w = palette_w - 1 end

  ui.clear(15)

  paper = ui.canvas(paper_w, paper_h)
  if paper == nil then
    console.print('paint: no room for the paper of a drawing')
    return false
  end
  paper:place(paper_x, paper_y)
  paper:clear(15, kPaperMode)

  palette = ui.canvas(palette_w, kPaletteH)
  if palette ~= nil then
    palette:place(palette_x, palette_y)
    palette.on_draw = function(self) paint_the_palette(self) end
  end

  -- **El peu, que diu el que la pàgina ha fet**: the last line of the glass, in the small face and the grey of a secondary word.
  note_label = ui.label(page_w - 2 * kMargin, kNoteH)
  if note_label ~= nil then
    note_label.text = note
    note_label.scale = kSmall
    note_label.color = 8
    note_label.valign = 'middle'
    note_label:place(kMargin, page_h - kMargin - kNoteH + 2)
  end

  local tools_y = kBarRow + 6
  brush_key = ui.button(kToolSide, kToolSide)
  if brush_key ~= nil then
    brush_key:place(kMargin, tools_y)
    brush_key.icon = kToolNames[kBrush]
    brush_key.primary = (now.tool == kBrush)
    brush_key.on_click = function(self) choose_the_tool(kBrush) end
  end
  bucket_key = ui.button(kToolSide, kToolSide)
  if bucket_key ~= nil then
    bucket_key:place(kMargin + kToolSide + kToolGap, tools_y)
    bucket_key.icon = kToolNames[kBucket]
    bucket_key.primary = (now.tool == kBucket)
    bucket_key.on_click = function(self) choose_the_tool(kBucket) end
  end

  -- **El gruix és un número que el dit mou** (0.192.34): a slider and not a row of four boxes, perquè el que el pinzell vol és un
  -- número qualsevol i no el que a mi em va semblar bé (docs/30, docs/90).
  local slider_x = kMargin + 2 * kToolSide + kToolGap + 12
  size_key = ui.slider(page_w - slider_x - kMargin, 44)
  if size_key ~= nil then
    size_key:place(slider_x, tools_y + (kToolSide - 44) // 2)
    size_key.min = 1
    size_key.max = 16
    size_key.step = 1
    size_key.value = now.size
    size_key.suffix = ' px'
    size_key.scale = kSmall
    size_key.on_change = function(self, value)
      now.size = math.max(1, math.min(16, math.floor(tonumber(value) or now.size)))
      save_state()
      -- **I el número es veu**: the control writes its field and asks the system for its own paint, and the paint happens at the
      -- next present; a page that never presents keeps the number it had on the glass and a person moves a slider that does
      -- nothing (found on the emulator, 0.192.47) (docs/30, docs/90).
      ui.present(kCheapMode)
    end
  end

  bar = ui.menu_bar(page_w, kBarRow)
  if bar ~= nil then
    bar.bar_height = kBarHeight
    bar:place(0, 0)
    -- **No hi ha cap `Open`** (0.192.47, l'amo: *«No posis l'opció "Open", ja es fà desde l'app Files o la comanda open»*): the door
    -- that opens a drawing is the system's —a row of `Files`, `run paint <path>` or `app.run('paint', true, nil, path)`— i la pàgina
    -- rep el camí a `app.args.path` i el dibuix torna al vidre. Dues portes per a la mateixa cosa serien dues maneres de preguntar on
    -- és un fitxer, i la del sistema ja sap què és un dibuix (el manifest diu `ics`) (docs/40, docs/45).
    bar.items = {
      { title = 'File', items = {
        { label = 'Save' },
        { label = 'Export' },
        '-',
        { label = 'Close' },
      } },
    }
    bar.on_pick = function(self, title, entry)
      if title ~= 1 then return end
      if entry == 1 then
        local path = (now.file ~= '') and now.file or nil
        if path == nil then
          local folder = the_folder()
          if folder == nil then
            say_the_note('there is no ground to save in')
            ui.present(kCheapMode)
            return
          end
          path = a_free_name(folder)
        end
        local what, why = save_the_paper(path)
        if what == nil then
          say_the_note(tostring(why or 'that did not save'))
        else
          say_the_note('saved ' .. the_name_only(what))
        end
        ui.present(kCheapMode)
      elseif entry == 2 then
        if now.file == '' then say_the_note('an export is written beside the drawing, so it is saved first') end
        local path = with_another_extension(now.file, 'pgm')
        if now.file == '' then
          local folder = the_folder()
          path = (folder ~= nil) and with_another_extension(a_free_name(folder), 'pgm') or nil
          if path ~= nil then
            local saved = save_the_paper(with_another_extension(path, kExtension))
            if saved == nil then path = nil end
          end
        end
        if path == nil then
          ui.present(kCheapMode)
          return
        end
        local what, why = export_the_paper(path)
        if what == nil then
          say_the_note(tostring(why or 'that did not export'))
        else
          say_the_note('exported ' .. the_name_only(what))
        end
        ui.present(kCheapMode)
      elseif entry == 4 then
        -- **La pàgina que marxa desa el dibuix abans de sortir** (0.192.47): the script's own return is not a close the shell hears,
        -- so the drawing is written here —the file it came from, or the draft— and the next start puts it back (docs/30, docs/75).
        keep_the_drawing()
        save_state()
        quitting = true
      end
    end
  end
  return true
end

function choose_the_tool(which)
  now.tool = which
  save_state()
  if brush_key ~= nil then brush_key.primary = (which == kBrush) end
  if bucket_key ~= nil then bucket_key.primary = (which == kBucket) end
  ui.present(kCheapMode)
end

-- **La pàgina, amb les caixes de cada cosa**: the card is placed in its own pixels and the rest here, which is what a present after a
-- menu needs —the bar of menus is as tall as the page while something hangs from it— (docs/30, docs/90).
function layout()
  if paper == nil then return end
  local now_view = ui.caps()
  page_w, page_h = now_view.width, now_view.height
  local wide, tall, top, palette_top = the_paper_room()
  if wide ~= paper_w or tall ~= paper_h then
    -- **Un canvas té una mida i no una altra**: a paper that has to change its size is another paper, and the drawing that was on
    -- it is kept by asking for a new block and copying the two (docs/30, docs/90).
    local keep = the_pixels()
    local had = keep ~= nil and paper:capture(keep, 0) ~= nil
    paper:release()
    paper = ui.canvas(wide, tall)
    if paper == nil then
      console.print('paint: no room for the paper of a drawing')
      return
    end
    paper:place(paper_x, top)
    paper:clear(15, kPaperMode)
    if had then paper:blit(keep, 0, 0, 0, wide, tall, kPaperMode) end
    paper_w, paper_h, paper_y = wide, tall, top
  else
    paper:place(paper_x, top)
  end
  palette_y = palette_top
  if palette ~= nil then
    palette:place(palette_x, palette_y)
    palette:invalidate()
  end
end

-- **El paper té una fila d'enquadernació** (docs/92): the page of a drawing and the paper of the app are both the fifteenth tone of
-- this glass, and a person who cannot see where the paper starts does not know where the brush begins. One pixel of the middle grey
-- round the paper is a control of nothing —it is ink on the page— and the hole of a menu would take it away, so it is painted again
-- every time the page is put back together (docs/30, docs/90).
local function paint_the_frame()
  if paper == nil then return end
  ui.frame(paper_x - 1, paper_y - 1, paper_w + 2, paper_h + 2, 8)
end

local function repaint_the_page()
  layout()
  paint_the_frame()
  -- **El paper es torna a compondre i no només es presenta**: the card of a menu is painted into the page's pixels over the paper, and
  -- the hole it leaves is paper and the controls that were under it —a control with a buffer is composed, but only the part of it
  -- the hole's repair asks for—. A paper asked again with its own buffer is the drawing that was there, which is what a person
  -- expects to come back (found on the emulator: a pale rectangle left over the drawing after a menu folded) (docs/30, docs/90).
  if paper ~= nil then paper:invalidate() end
  if palette ~= nil then palette:invalidate() end
  ui.present(kPaperMode)
end

-- ---------------------------------------------------------------- the loop, which samples the finger and waits

local open_was = 0
local bar_tall = false

local function in_the_paper(x, y)
  return x >= paper_x and x < paper_x + paper_w and y >= paper_y and y < paper_y + paper_h
end

local function in_the_palette(x, y)
  return palette ~= nil and x >= palette_x and x < palette_x + palette_w and y >= palette_y and y < palette_y + kPaletteH
end

local function pick_a_level(x)
  local level = level_of_a_swatch(x)
  if level == nil or level == now.level then return end
  now.level = level
  save_state()
  if palette ~= nil then
    palette:invalidate()
    ui.present(kCheapMode)
  end
end

-- **El que hi ha a la carpeta d'aquesta app, dit al prompt**: a page that saves somewhere nobody can see is a page that hides its
-- drawings. `/data/paint/drawings` és el que `Files` ensenya i el que el sistema fa servir per a la cara de cada fitxer (docs/70).
local folder = the_folder()
console.print('paint: drawings go to ' .. tostring(folder or '(no ground)') .. '; a drawing is an .ics, the cheapest picture here')

if not create_the_page() then
  return
end
-- **El dibuix que es va obrir amb l'app torna al vidre** (`app.args.path`, docs/30): a row of `Files` that picks a drawing opens this
-- page with it, and the paper is that drawing before the first finger arrives. **I el que es va quedar a mitges torna sol**: a page
-- that pauses or closes keeps its drawing in a draft (`/data/paint/drawings/draft.ics`) because a drawing is a document and not the
-- shape of a view, and the next start puts it back —a draft is not a name, o sigui que `Save` tornarà a preguntar on va quan el nom
-- encara no és seu— (docs/30, docs/75).
local opened = false
if app.args ~= nil and app.args.path ~= nil and app.args.path ~= '' then
  local what, why = open_the_drawing(app.args.path)
  opened = what ~= nil
  if what == nil then say_the_note(tostring(why or 'the drawing did not open')) end
end
if not opened and folder ~= nil then
  local draft = folder .. '/draft.' .. kExtension
  if fs.stat(draft) ~= nil then
    local what, why = open_the_drawing(draft)
    if what ~= nil then
      now.file = ''  -- the draft is the drawing that has no name yet
      save_state()
    else
      say_the_note(tostring(why or 'the draft did not open'))
    end
  end
end
repaint_the_page()

while not quitting do
  local open = (bar ~= nil) and (bar.open or 0) or 0
  local just_changed = false
  if open ~= open_was then
    open_was = open
    just_changed = true
    local tall = open > 0
    if tall ~= bar_tall then
      bar_tall = tall
      if bar ~= nil then bar:resize(page_w, tall and page_h or kBarRow) end
    end
    -- **Un menú que es plega o s'obre és la pàgina tornada a fer**: the card is painted into the page and its hole is paper plus the
    -- controls that were under it; the paper of this app and its frame are put back here, which is the same thing the editor of
    -- this system does with its own page when a menu changes (docs/30, docs/90).
    repaint_the_page()
  end

  -- **Mentre la targeta d'un menú és a sobre, el dit és seu** (docs/30): the card of the bar hangs over the top of the paper, and a
  -- finger on an entry —`Close` is the third line, and it is over the drawing— would be a finger on the drawing too: the stroke would
  -- be drawn under the card a person is reading. The gesture is taken here and not done, because a queue nobody empties grows.
  local menu_up = open > 0 or just_changed
  do
    local fingers = touch.read()
    local stamp = sys.time_ms()
    local on_the_paper = false
    -- **Un toc és un gest i no un contacte** (0.192.47): a finger that lands and lifts inside one turn of this page —quaranta
    -- mil·lisegons quan no es dibuixa, i un toc ràpid és més curt que això— no el veu mai `touch.read()`, perquè quan el bucle mira
    -- ja s'ha aixecat. El gest sí que hi és, i és el que fa que una mostra del to o un punt al paper responguin al primer intent
    -- (docs/30, docs/90). **I la cua s'ha de buidar sempre**, o un toc vell es pren quan ja no vol dir res.
    local gesture = touch.gesture()
    if menu_up then
      -- the fingers of this turn belong to the card: a stroke that was open is closed where it was and nothing is drawn from here
      if drawing then
        drawing = false
        last_move = stamp
        local stroke = strokes[#strokes]
        if stroke ~= nil then stroke.open = false end
      end
      gesture = nil
      fingers = {}
    end
    if gesture ~= nil and gesture.kind == 'tap' then
      if in_the_palette(gesture.x, gesture.y) then
        pick_a_level(gesture.x)
      elseif in_the_paper(gesture.x, gesture.y) and not drawing then
        -- a tap on the paper is a dot, and it waits for the same quiet as a stroke
        start_a_stroke(gesture.x - paper_x, gesture.y - paper_y)
        local stroke = strokes[#strokes]
        if stroke ~= nil then stroke.open = false end
        last_move = stamp
      end
    end
    if #fingers > 0 then
      local finger = fingers[1]
      if in_the_paper(finger.x, finger.y) then
        on_the_paper = true
        if not drawing then
          drawing = true
          start_a_stroke(finger.x - paper_x, finger.y - paper_y)
        end
        add_a_point(finger.x - paper_x, finger.y - paper_y)
        last_move = stamp
      elseif not drawing and in_the_palette(finger.x, finger.y) then
        if not picked then
          picked = true
          pick_a_level(finger.x)
        end
      end
    end
    if drawing and not on_the_paper then
      -- **El dit ha marxat del paper**: the stroke is closed where it was and the quiet starts counting from here.
      drawing = false
      last_move = stamp
      local stroke = strokes[#strokes]
      if stroke ~= nil then stroke.open = false end
    end
    if #fingers == 0 then
      if drawing then
        drawing = false
        last_move = stamp
        local stroke = strokes[#strokes]
        if stroke ~= nil then stroke.open = false end
      end
      picked = false
    end
    -- **I el dibuixat, quan el vidre ha estat quiet** (l'amo: *«passats 2,3 segons»*): the pause is the page's and not the system's,
    -- i un dit que torna abans no ha perdut res (docs/30, docs/50).
    if not drawing and #strokes > 0 and (stamp - last_move) >= kIdleMs then
      paint_the_strokes()
      note = ''
    end
    sys.delay(drawing and kStep or kQuiet):await()
  end
end

-- ---------------------------------------------------------------- the end of the page

function on_pause()
  keep_the_drawing()
  save_state()
end

function on_save()
  keep_the_drawing()
  save_state()
end

function on_close()
  keep_the_drawing()
  save_state()
end

-- **Una pàgina que marxa deixa paper net**: the controls go first, because a bar that still owes pixels paints itself over the wipe
-- (docs/30, docs/90).
local kids = ui.root().children
for i = #kids, 1, -1 do kids[i]:release() end
ui.clear(15)
ui.present('gld16')
console.print('paint: closed')
