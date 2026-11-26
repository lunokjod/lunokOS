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
--   - **`Export` escriu un `PNG` de vuit bits de gris, i el fa el sistema** (0.192.51): `paper:export(path)` dona el canvas i el
--     camí, i el nucli —que ja té els píxels i l'escriptor de la llibreria d'imatges— escriu el fitxer. Abans era un PGM escrit fila a
--     fila des de Lua, que és el que penjava l'app (l'amo: *«Si exportar la imatge a png es massa feixuc per l'app lua, que ho faci el
--     sistema (li passes el canvas i ell guarda)»*, docs/30, docs/75).
--   - **Les tres portes del nom, i què fa cadascuna** (0.192.53): `Save` desa **al nom que el dibuix ja té** i només pregunta quan no
--     en té; `Save as` pregunta sempre —és la porta per a un altre nom, i el camp comença amb el d'ara—; i `Export` pregunta sempre i
--     suggereix **el nom del dibuix**, perquè el que canvia és només l'extensió i el PNG queda al costat del `.ics` (l'amo: *«Al
--     polsar "Export" d'un arxiu ja guardat, usa el mateix nom per l'arxiu exportat (l'extensiò es diferent)»* i *«A exportar sempre
--     pregunt nom, només suggereix el mateix nom que s'ha guardat»*) (docs/30, docs/90).
--   - **Les eines són les del sistema**: el pinzell i el cubell són dues marques de `tools/iconart.py` (`paint-brush` i
--     `fill-drip`), com la casa, la roda i la creu de la targeta de la barra, i la paleta és **els setze tons que el vidre té** —un
--     color i no setze és una mentida d'aquest panell— (docs/45, docs/92).
--   - **El pinzell arriba als seixanta-quatre píxels** (0.192.56, l'amo: *«El tamany del pinzell de l'app Paint ha de ser un rang mes
--     gran, el pinzell ha de poder ser molt mes gruixut, 16px no es suficient»*): the slider was a range of sixteen and the widest
--     brush it could ask for was a hair on a paper of five hundred; what a person paints with is a range that reaches a third of the
--     paper (docs/30, docs/90).
--   - **`File → New` i `Edit → Undo`** (0.192.56): a white paper that keeps nothing of the one before it —the drawing goes to its
--     file first— and **un sol pas enrere**, perquè el paper és un canvas i un canvas no té història: el que es desa abans de pintar és
--     una captura del paper en una sessió que es diu `undo`, i tornar-hi és un `blit` d'aquell bloc (docs/30, docs/75).
--   - **I el dibuix que s'està pintant es recorda** (0.192.56): el seu nom viu a la sessió `status` i no a `state`, que el nucli
--     esborra a cada arrencada de debò —una pàgina que tornés a l'esborrany d'un altre dibuix seria pitjor que una pàgina en blanc—
--     (docs/30, docs/75).

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
local kAskPad = 18                             -- the room the ask's page gives its words from the edge of the glass
local kNoteH = 22                            -- the line of the foot, where the page says what it has just done

-- **La quietud que vol dir «la mà ha marxat»** (l'amo: *«passats 2,3 segons»*): two and a half seconds, and it is the number of
-- this page and not of the system —a person who lifts a finger and puts it down again inside it is still drawing the same thing—.
local kIdleMs = 2500
local kPaperMode = 'gc16'  -- a drawing is a picture and not text: every pass of it is clean, not cheap
local kCheapMode = 'du'
local kStep = 2            -- the turn of the loop while a finger is on the glass: the chip reports every ten (docs/50)
local kQuiet = 40          -- and the turn when nobody is drawing, which is the one that costs nothing
local kStrokeCap = 4000    -- the points of one stroke: a digit that stays on the glass a minute must not fill the heap
-- **El pinzell més gruixut** (l'amo: *«El tamany del pinzell de l'app Paint ha de ser un rang mes gran, el pinzell ha de poder ser
-- molt mes gruixut, 16px no es suficient»*): sixty-four pixels are a third of the paper, which is what a person painting with a
-- finger asks for; the stroke costs a disc every half radius and a stroke of sixty-four is still a handful of them (docs/30, docs/90).
local kThickest = 64

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
-- **v2** (0.192.56): the page reads its `level` again —the block is a kilobyte of zeros after the text, and a value with zeros
-- behind it is not a number (`tonumber` says `nil`)— so the shape of v1 is thrown away and the next one is written whole.
local kLayout = 'v2'
local state_block = nil

-- **Un bloc que el sistema s'ha quedat no es pot tornar a fer servir** (0.192.55): the session blocks are the first thing the
-- allocator gives back when the memory is short, and **desar un dibuix és justament el moment de pressió** —la captura dels píxels
-- demana cent vuitanta kilobytes—. O sigui que el bloc de la forma pot morir mentre la pàgina viu, i `clear()` sobre un bloc que se
-- n'ha anat aixeca `the session memory was given back` i l'app peta (vist a la placa: `paint.lua:114: the session memory was given
-- back`, amb la pàgina a sobre del vidre). Es pregunta `alive()` —que no costa res— i si no ho és es demana un bloc nou, que és el
-- que diu el contracte (docs/30, docs/75).
local function the_state()
  if state_block ~= nil and not state_block:alive() then state_block = nil end
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
  -- **I es torna a preguntar just abans d'escriure**: entre el moment en què es demana el bloc i el moment d'escriure-hi hi ha la
  -- feina de la pàgina, i el bloc pot haver desaparegut entremig (0.192.55, docs/30).
  if not block:alive() then
    state_block = nil
    block = the_state()
    if block == nil then return false end
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
  -- **El bloc és més llarg que el text** (0.192.56): what follows the last line is the zero the block was made of, and a value with
  -- zeros behind it is not a value —`tonumber('2\0\0')` is `nil`—, o sigui que la darrera línia no es llegia mai i el to de la
  -- paleta tornava al primer cada cop (the same trim the reader has, docs/30).
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
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
  now.size = math.max(1, math.min(kThickest, math.floor(tonumber(now.size) or 3)))
  now.level = math.max(0, math.min(15, math.floor(tonumber(now.level) or 0)))
  now.file = ''
end

read_state()

-- ---------------------------------------------------------------- the drawing that is being painted (`status`, docs/30, docs/75)

-- **El dibuix que s'està pintant és un document i no la forma de la vista** (l'amo: *«Paint ha de recordar l'imatge que està pintant
-- amb session status»*): `state` el nucli l'esborra a cada arrencada de debò, o sigui que el nom del dibuix no hi pot viure i la
-- pàgina tornava al **esborrany** —que és el dibuix de l'última vegada que no tenia nom—, i el que la persona veia era un dibuix
-- vell en lloc del seu. El nom viu en una sessió que es diu `status`, que el nucli desa a la targeta abans d'apagar la placa i torna
-- a llegir quan l'app arrenca, i és la mateixa mena de bloc que el `pixels` d'aquesta pàgina (docs/30, docs/75).
local kStatusBytes = 256
local kStatusLayout = 'v1'
local status_block = nil

local function the_status()
  if status_block ~= nil and not status_block:alive() then status_block = nil end
  if status_block == nil then
    local block = sys.session('status', kStatusBytes)
    if block == nil then return nil end
    status_block = block
  end
  return status_block
end

-- **El nom que es recorda és `now.file` i prou**: an unnamed drawing is the draft, and the draft is already a file that the page
-- opens by itself —remembering `draft.ics` as if it were a name would make the next `Save` write over it without asking—.
local function remember_the_drawing()
  local block = the_status()
  if block == nil then return false end
  if not block:alive() then
    status_block = nil
    block = the_status()
    if block == nil then return false end
  end
  block:clear()
  block:write(0, kStatusLayout .. '\nfile=' .. escape(now.file or ''))
  return true
end

local function the_drawing_remembered()
  local block = the_status()
  if block == nil then return '' end
  local text = block:read(0, kStatusBytes) or ''
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  local first = true
  for line in text:gmatch('[^\n]+') do
    if first then
      first = false
      if line ~= kStatusLayout then return '' end
    else
      local key, value = line:match('^([%w_]+)=(.*)$')
      if key == 'file' then return unescape(value) end
    end
  end
  return ''
end

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

-- **La pàgina que demana un nom** (0.192.51, l'amo: *«Save no mostra una vista per guardar amb nom ... Usa una vista nova, no facis un
-- modal»*): `asking` és `'save'`, `'save_as'`, `'export'` o nil, i mentres no sigui nil la pàgina del dibuix és a sota d'un full blanc
-- amb el camp del nom i les dues portes. **No és una targeta del sistema**: és una pàgina d'aquesta app, com la del terminal del `shell`,
-- i el que hi ha darrere no és mig visible —el dibuix no es toca ni es torna a compondre (docs/30, docs/90).
local asking = nil
-- **El marc del paper es nomena abans d'existir** (0.192.51): `close_the_ask` el torna a pintar i viu més amunt en aquest fitxer, i un
-- `local function` només existeix per a les funcions que es defineixen després (docs/30).
local paint_the_frame
local asking_owned = {}
local ask_sheet, ask_title, ask_field, ask_note, ask_cancel, ask_ok = nil, nil, nil, nil, nil, nil
local wanting = nil     -- el que el menú ha demanat i el bucle ha d'obrir, fora del ganxo del menú
local ask_wish = nil    -- el que la pàgina del nom ha demanat ('ok' o 'cancel'), també fora del ganxo del botó
local ask_closed_at = 0 -- quan s'ha plegat la pàgina del nom, per no prendre el gest del botó com un toc al paper
local do_this = nil     -- la feina que el bucle ha de fer (desar o exportar), també fora dels ganxos
local keyboard = nil

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

-- ---------------------------------------------------------------- the one step back (`undo`, docs/30, docs/75)

-- **Un pas enrere i no una pila** (l'amo: *«Al Menú contextual de Paint li falta … Edit -> Undo (per desfer l'últim traç)»*): the
-- paper is a canvas and a canvas has no history —what the stroke that is about to be painted covers is not written down anywhere—,
-- o sigui que el que es desa **abans** de pintar és la captura del paper, i tornar-hi és un `blit` d'aquell bloc. Un bloc de la
-- mida del paper, i prou: dues captures de cent vuitanta kilobytes serien una pila que aquesta placa no té per a un dibuix.
local kUndoKey = 'undo'
local undo_block = nil
local undo_bytes = 0
local undo_ready = false

local function forget_the_undo()
  local had = undo_block ~= nil or undo_bytes > 0
  undo_block, undo_bytes, undo_ready = nil, 0, false
  -- **I es llença del disc i no es desa** (docs/75): a block that lives is parked to the card on every pause, and a scratch the size
  -- of the paper is half a second of writing per app that comes and goes.
  if had then sys.session_destroy(kUndoKey) end
end

local function take_the_undo()
  if paper == nil then return false end
  local bytes = (paper_w // 2) * paper_h
  if bytes <= 0 then return false end
  if undo_block ~= nil and not undo_block:alive() then undo_block, undo_bytes, undo_ready = nil, 0, false end
  if undo_block == nil or undo_bytes < bytes then
    undo_block = nil
    local block = sys.session(kUndoKey, bytes)
    -- **Una memòria curta vol dir que aquesta pàgina no té pas enrere** i no que no pugui dibuixar: the stroke is painted and the
    -- `Undo` of the menu will say there is nothing to take back (docs/30, docs/75).
    if block == nil then return false end
    undo_block, undo_bytes = block, bytes
  end
  if not undo_block:alive() then
    undo_block, undo_bytes, undo_ready = nil, 0, false
    return false
  end
  undo_ready = paper:capture(undo_block, 0) ~= nil
  return undo_ready
end

local function undo_the_paper()
  if not undo_ready or undo_block == nil then return false end
  if not undo_block:alive() then
    undo_block, undo_bytes, undo_ready = nil, 0, false
    return false
  end
  if paper:blit(undo_block, 0, 0, 0, paper_w, paper_h, kPaperMode) == nil then return false end
  undo_ready = false  -- one step: what the stroke covered is back, and the step before it was never kept
  paper:invalidate()  -- the present is the page's, once, when the note is said
  return true
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

-- **Cada mostra porta el seu mil·lisegon** (l'amo: *«Guarda el temps de cada mostra, així podrem saber si el traç era ràpid o lent»*):
-- the touch of this board gives a sample every ten milliseconds —and repeats it, which `add_a_point` drops— so the time of a point
-- is the only thing that tells a hand that was resting from one that was flying: a finger that walks a pixel every hundred
-- milliseconds and one that crosses six pixels every ten are the same two points on the glass and two different hands. **El temps no
-- va al fitxer**: un `.ics` és una imatge i una imatge no té mostres (docs/46, docs/90), o sigui que el rellotge d'un traç viu
-- mentres el traç s'està pintant.
local function start_a_stroke(x, y, t)
  if #strokes > 0 and strokes[#strokes].open then return end
  local points = { { x, y, t } }
  strokes[#strokes + 1] = { level = now.level, size = now.size, tool = now.tool, open = true, points = points }
end

local function add_a_point(x, y, t)
  local stroke = strokes[#strokes]
  if stroke == nil then return end
  local points = stroke.points
  local last = points[#points]
  if last ~= nil and math.abs(last[1] - x) < 1 and math.abs(last[2] - y) < 1 then return end
  if #points >= kStrokeCap then return end
  points[#points + 1] = { x, y, t }
end

-- **El suavitzat, i com de fort és** (l'amo: *«Suavitzarem mes o menys les corbes en funció a la distancia i la velocitat entre
-- mostres»*): the samples of a slow hand land a pixel apart, on whole pixels, and the panel jitters inside one of them —so a line
-- through them is a staircase, and that staircase is the wobble of a finger that is barely moving—. Pulling every point a little
-- towards the middle of its two neighbours takes it away. A fast hand leaves samples ten or twenty pixels apart: there is nothing
-- between them to tidy, and pulling one of them would move the line where the finger did not go, so the weight falls to zero and
-- the stroke is the polyline the hand drew. **El pes és la més prudent de les dues lectures** —el pas en píxels i la velocitat en
-- píxels per segon— perquè que una de les dues sigui gran vol dir que el dit anava enlloc.
local kDenseStep, kSparseStep = 3, 15    -- px between two samples: dense is a hand at rest, sparse is a hand in a hurry
local kSlowSpeed, kFastSpeed = 80, 700   -- and the same two ends in px/s (a sample every ten milliseconds, docs/50)
local kMostSmoothing = 0.85              -- the most a point may be pulled of the way to the middle of its neighbours

local function clamp01(v)
  if v < 0 then return 0 end
  if v > 1 then return 1 end
  return v
end

-- **Quant es mou un punt, de 0 a `kMostSmoothing`**: it reads the sample before it —the step it took and the time it took to
-- happen— and that is the whole of the clock the file does not carry. Un punt del mig d'una corba lenta es mou, i com més lenta i
-- més juntes les mostres, més; i el primer i l'últim no es mouen mai, que són on el dit va tocar i on va deixar de tocar.
local function the_smoothing(points, i)
  local a, b = points[i - 1], points[i]
  local dx, dy = b[1] - a[1], b[2] - a[2]
  local step = math.sqrt(dx * dx + dy * dy)
  local ms = math.max(1, (b[3] or 0) - (a[3] or 0))
  local speed = step * 1000 / ms
  local by_step = clamp01((kSparseStep - step) / (kSparseStep - kDenseStep))
  local by_speed = clamp01((kFastSpeed - speed) / (kFastSpeed - kSlowSpeed))
  return kMostSmoothing * math.min(by_step, by_speed)
end

-- **Un tros de traç** (docs/30): the canvas has a line of one pixel and a disc of a radius, and the line of a pen of four is the
-- discs the pen leaves when it walks.
local function paint_a_bit(canvas, ax, ay, bx, by, radius, level)
  if radius < 1 then
    canvas:line(ax, ay, bx, by, level, kPaperMode)
    return
  end
  local dx, dy = bx - ax, by - ay
  local distance = math.sqrt(dx * dx + dy * dy)
  -- **El pas és mig radi i no el radi sencer** (0.192.56): two discs of radius r whose centres are r apart touch, but the envelope
  -- between them dips by 0,134·r —a pixel and a half with the old ceiling of sixteen, and **four with the new one of sixty-four**,
  -- which is the caterpillar a thick brush was leaving on the paper (seen on the emulator). Half a radius takes the dip to 0,03·r,
  -- which is a third of a pixel at the widest, i el preu és el doble de discs en un tros de traç (docs/30, docs/90).
  local n = math.max(1, math.ceil(distance / math.max(1, radius / 2)))
  for k = 0, n - 1 do
    local t = k / n
    canvas:disc(math.floor(ax + dx * t + 0.5), math.floor(ay + dy * t + 0.5), radius, level, kPaperMode)
  end
end

local function paint_a_stroke(stroke, canvas)
  local radius = math.floor(stroke.size / 2)
  local points = stroke.points
  local n = #points
  local first = points[1]
  if n < 2 then
    if radius < 1 then
      canvas:pixel(first[1], first[2], stroke.level, kPaperMode)
    else
      canvas:disc(first[1], first[2], radius, stroke.level, kPaperMode)
    end
    return
  end
  -- **Es camina del primer punt a l'últim amb el punt suavitzat a la mà** i no es fa cap còpia del traç: quatre mil mostres són
  -- quatre mil taules, i el munt té un sostre (docs/75). O sigui que el dibuixat llegeix els tres punts que necessita, pinta el tros
  -- i passa al següent.
  local px, py = first[1], first[2]
  local travel = 0
  for i = 2, n - 1 do
    local p = points[i]
    local nx, ny = p[1], p[2]
    local w = the_smoothing(points, i)
    if w > 0 then
      nx = nx + ((points[i - 1][1] + points[i + 1][1]) / 2 - nx) * w
      ny = ny + ((points[i - 1][2] + points[i + 1][2]) / 2 - ny) * w
    end
    paint_a_bit(canvas, px, py, nx, ny, radius, stroke.level)
    local dx, dy = nx - px, ny - py
    travel = travel + math.sqrt(dx * dx + dy * dy)
    px, py = nx, ny
  end
  -- the last one, so a stroke that stops does not lose its tip
  local tip = points[n]
  paint_a_bit(canvas, px, py, tip[1], tip[2], radius, stroke.level)
  if radius < 1 then
    canvas:pixel(tip[1], tip[2], stroke.level, kPaperMode)
  else
    canvas:disc(tip[1], tip[2], radius, stroke.level, kPaperMode)
  end
  local dx, dy = tip[1] - px, tip[2] - py
  travel = travel + math.sqrt(dx * dx + dy * dy)
  -- **I el pas del traç surt al registre** (l'amo: *«així podrem saber si el traç era ràpid o lent»*): the two numbers a smoothing
  -- is judged by, said once per stroke —samples, how long the hand took to make them and how fast it was going— so a drawing that
  -- comes out looking wrong tells whether the hand or the weights were the ones that were off (docs/85).
  local span = math.max(0, (tip[3] or 0) - (first[3] or 0))
  if span > 0 then
    console.print(string.format('paint: %d samples in %d ms, %d px at %.0f px/s', n, span,
                                math.floor(travel + 0.5), travel * 1000 / span))
  end
end

-- **El dibuixat, d'un sol cop** (l'amo: *«Un cop el dit s'hagi aixecat … es fa el dibuixat»*): everything the fingers left is painted
-- and the glass is presented once, which is the whole difference between this page and a canvas that follows a hand (docs/30, docs/50).
local function paint_the_strokes()
  if paper == nil or #strokes == 0 then return end
  -- **El paper tal com és abans d'aquests traços** (0.192.56): it is what `Edit → Undo` gives back, and it is taken here because
  -- this is the only place where the paper changes (the strokes are painted and thrown away, docs/30).
  take_the_undo()
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
    -- **Aquí no es cedeix el torn** (0.192.51): this file is written from the three doors of the shell —`on_pause`, `on_save`,
    -- `on_close`—, which are not the coroutine of the page and where a `sys.delay():await()` is an error, o sigui que el que hi
    -- ha és el que ja hi era: quaranta-sis trossos de quatre kilobytes en una volta. La que sí que cedeix és l'exportació, que és
    -- vint vegades més feina (docs/20, docs/75).
  end
  file:close()
  now.file = path
  -- **El bloc dels píxels és un esborrany i es torna** (0.192.47): a session block of a hundred and eighty kilobytes is parked to the
  -- card on every pause —half a second of writing for a scratch— and what it holds is the drawing that is already in the file it was
  -- just written to. Asked again when it is needed, which is a capture and not a read (docs/75).
  drop_the_pixels()
  return path, nil
end

-- **`Export` el fa el sistema** (0.192.51, l'amo: *«Si exportar la imatge a png es massa feixuc per l'app lua, que ho faci el sistema
-- (li passes el canvas i ell guarda)»*): abans aquesta pàgina llegia el bloc dels píxels, fila a fila, i escrivia un PGM —cinc-centes
-- quaranta files i cinc-centes quaranta voltes de Lua, que és el que penjava l'app—. Ara és `paper:export(path)`: el canvas ja és al
-- nucli i el PNG de vuit bits de gris el fa ell, amb una crida i sense que l'app toqui ni un byte (docs/30, docs/75).
local function export_the_paper(path)
  if paper == nil then return nil, 'there is no paper to export' end
  local what, why = paper:export(path)
  if what == nil then return nil, tostring(why or 'the picture did not write') end
  return path, nil
end

-- **El nom del dibuix, i si el camí és un nom o un esborrany** (0.192.56): a draft is the drawing nobody has named yet, and
-- remembering `draft.ics` as a name would make the next `Save` write over it without asking (docs/30, docs/75).
local function open_the_drawing(path, named)
  if paper == nil then return nil, 'there is no paper' end
  paper:clear(15, kPaperMode)
  local ok, why = paper:image(0, 0, path, paper_w, paper_h, kPaperMode)
  if ok == nil then return nil, tostring(why or 'the picture did not open') end
  now.file = (named == false) and '' or path
  forget_the_undo()  -- the step back was of the paper that was here, and it is not this one
  remember_the_drawing()
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
  local named = now.file
  local path = (named ~= '') and named or (ground .. '/draft.' .. kExtension)
  local what, why = save_the_paper(path)
  -- **Un esborrany no és un nom** (0.192.53): `save_the_paper` es queda el camí que ha escrit, i el d'un esborrany no és el nom del
  -- dibuix —una persona no ha escrit mai `draft`—: si s'hi quedés, el següent `Export` exportaria un `draft.png` sense preguntar res
  -- i el següent `Save` tornaria a proposar `draft` (docs/30, docs/75).
  now.file = named
  remember_the_drawing()
  if what == nil then say_the_note('the drawing was not kept: ' .. tostring(why or '?')) end
end

-- **La pàgina diu el que ha fet** (docs/30): a drawing that is saved with no word back is a page that nobody knows has saved it, i el
-- peu és una línia de la cara petita com la de l'editor d'aquest sistema. **I el peu es neteja abans de dir res** (0.192.53): `du`
-- pinta el negre nou i no esborra el vell, o sigui que dues notes seguides es llegien l'una a sobre de l'altra —vist a l'emulador amb
-- `exported vasa.png` i `saved vasa.ics` al mateix lloc—; una passada neta del peu costa una tira de vint-i-dos píxels (docs/30).
function say_the_note(text)
  note = tostring(text or '')
  if note_label ~= nil then note_label.text = note end
  console.print('paint: ' .. note)
  if note_label ~= nil then
    note_label:invalidate()
    ui.present(kPaperMode)
  end
end

-- **No hi ha cap targeta que demani un camí** (0.192.47, l'amo: *«No posis l'opció "Open", ja es fà desde l'app Files o la comanda
-- open»*): the page is a drawing and three doors —save it, export it, leave it— i el dibuix que s'obre ve del sistema. El que hi havia
-- aquí era el full d'un camí amb el seu teclat, i sense `Open` al menú no hi ha res que el demani (docs/30, docs/40).

-- ---------------------------------------------------------------- la pàgina que demana un nom

local function keep_ask(control)
  if control ~= nil then asking_owned[#asking_owned + 1] = control end
  return control
end

-- **El nom que s'escriu no és un camí** (0.192.51): l'usuari escriu el nom del dibuix i prou, i el full on es desa i l'extensió
-- els posa l'app —un camp on hi cap `/` i `.ics` és un camp on una persona pot fer desaparèixer un dibuix en un altre lloc (docs/30).
local function a_name_from(text, extension)
  local name = tostring(text or ''):gsub('^%s+', ''):gsub('%s+$', '')
  name = name:gsub('[/\\]', '-')
  name = name:gsub('%.[%w]+$', '')
  if name == '' then return nil end
  return name .. '.' .. extension
end

local function layout_the_ask()
  local view = ui.caps()
  page_w, page_h = view.width, view.height
  local width = page_w - 2 * kMargin
  if ask_sheet ~= nil then
    ask_sheet:resize(page_w, page_h)
    ask_sheet:place(0, 0)
  end
  local inner = width - 2 * kAskPad
  local y = kAskPad
  if ask_title ~= nil then
    ask_title:resize(inner, ui.line_height(kBody) + 8)
    ask_title:place(kMargin + kAskPad, y)
    y = y + ask_title:caps().height + 18
  end
  if ask_field ~= nil then
    ask_field:resize(inner, ui.line_height(kBody) + 18)
    ask_field:place(kMargin + kAskPad, y)
    y = y + ask_field:caps().height + 10
  end
  if ask_note ~= nil then
    ask_note:resize(inner, ui.line_height(kSmall) + 6)
    ask_note:place(kMargin + kAskPad, y)
    y = y + ask_note:caps().height + 18
  end
  local half = (inner - 24) // 2
  if ask_cancel ~= nil then
    ask_cancel:resize(half, ui.line_height(kBody) + 14)
    ask_cancel:place(kMargin + kAskPad, y)
  end
  if ask_ok ~= nil then
    ask_ok:resize(half, ui.line_height(kBody) + 14)
    ask_ok:place(kMargin + kAskPad + half + 24, y)
  end
end

local function close_the_ask()
  -- **El toc que ha premut el botó no és un toc del paper** (0.192.51): the finger that says Save is still in the queue when the page
  -- under it comes back, i el bucle del dibuix el veuria com un punt a la pàgina —un puntet dibuixat a sota del botó que s'acaba de
  -- prémer (vist a l'emulador)—. La cua es buida aquí, que és el mateix que fa el bucle amb el menú (docs/30, docs/90).
  while touch.gesture() do end
  -- **I el gest del botó encara ha d'arribar** (0.192.51): the system turns the contact into a tap about a third of a second after the
  -- finger lifted, o sigui que en aquest moment la cua és buida i el toc arriba després, quan el paper ja és a sota. La marca és el
  -- que fa que el bucle no el prengui per un punt dibuixat (vist a l'emulador, docs/30, docs/90).
  ask_closed_at = sys.time_ms()
  if keyboard ~= nil then keyboard:hide() end
  for i = #asking_owned, 1, -1 do
    local one = asking_owned[i]
    if one ~= nil then pcall(function() one:release() end) end
  end
  asking_owned = {}
  ask_sheet, ask_title, ask_field, ask_note, ask_cancel, ask_ok = nil, nil, nil, nil, nil, nil
  asking = nil
  -- **El dibuix torna sencer** (0.192.51): the white sheet and the ask's controls are gone, and the paper, the palette, the keys and
  -- the frame are asked for again —the sheet is painted into the page's pixels, and a control with its own buffer is the only thing
  -- that comes back by itself— (docs/30, docs/90).
  ui.clear(15)
  layout()
  paint_the_frame()
  if paper ~= nil then paper:invalidate() end
  if palette ~= nil then palette:invalidate() end
  ui.present(kPaperMode)
end

-- **El botó no fa la feina ni es tanca a si mateix** (0.192.51): a control that releases the page it lives in from its own callback
-- is a slot the system is still painting —the shell wrote that lesson down for its own card— o sigui que el botó deixa el desig i el
-- bucle tanca la pàgina, escriu el fitxer i diu com ha anat (docs/30, docs/90).
local function confirm_the_ask()
  local kind = asking
  local extension = (kind == 'export') and 'png' or kExtension
  local name = a_name_from((ask_field ~= nil) and ask_field.text or '', extension)
  if name == nil then
    if ask_note ~= nil then ask_note.text = 'a drawing needs a name' end
    ui.present(kCheapMode)
    return
  end
  local folder = the_folder()
  if folder == nil then
    if ask_note ~= nil then ask_note.text = 'there is no ground to write in' end
    ui.present(kCheapMode)
    return
  end
  -- **`Save as` és una manera de desar i no una altra feina** (0.192.53): el nom que s'hi escriu fa un `.ics` com el de `Save`, i el
  -- que el bucle ha de fer és exactament el mateix —el nom és el que canvia, no el treball—.
  do_this = { kind = (kind == 'export') and 'export' or 'save', path = folder .. '/' .. name }
  ask_wish = 'cancel'  -- the page folds; the work is the loop's
end

-- **La pàgina es fa quan el menú ja ha tornat** (0.192.51): the bar of menus is a control this page gives back to make room for the
-- question, and giving back the control that is running its own callback is a door the system asks for afterwards —the same lesson
-- the shell wrote down for its session name (docs/30, docs/90).
local function open_the_ask(kind)
  if paper == nil then return end
  asking = kind
  local view = ui.caps()
  page_w, page_h = view.width, view.height
  -- The sheet goes over the drawing and the ask's controls over the sheet: the order they are made in is the order they are painted.
  ask_sheet = keep_ask(ui.label(page_w, page_h))
  if ask_sheet ~= nil then
    ask_sheet.background = 15
    ask_sheet.color = 15
    ask_sheet.text = ''
  end
  ask_title = keep_ask(ui.label(page_w - 2 * kMargin, ui.line_height(kBody) + 8))
  if ask_title ~= nil then
    ask_title.text = (kind == 'export') and 'Export the drawing'
                         or (kind == 'save_as') and 'Save the drawing as'
                         or 'Save the drawing'
    ask_title.scale = kBody
    ask_title.align = 'left'
  end
  ask_field = keep_ask(ui.textbox(page_w - 2 * kMargin, ui.line_height(kBody) + 18))
  if ask_field ~= nil then
    -- **El camp comença amb el nom que el dibuix ja té** (0.192.53, l'amo: *«A exportar sempre pregunt nom, només suggereix el mateix
    -- nom que s'ha guardat»*): `Export` perquè el que canvia és només l'extensió, i `Save as` perquè el que es fa és escurçar o allargar
    -- aquell nom i no tornar-lo a escriure —un dibuix sense nom no en té cap i el que hi va és el primer de lliure— (docs/30).
    local had = the_name_only(now.file)
    if had ~= '' then
      ask_field.text = had:gsub('%.[%w]+$', '')
    else
      local folder = the_folder()
      local free = (folder ~= nil) and the_name_only(a_free_name(folder)) or ('draw.' .. kExtension)
      ask_field.text = free:gsub('%.[%w]+$', '')
    end
    ask_field.cursor = #ask_field.text
    ask_field.scale = kBody
    ask_field.keyboard = 'alpha'
  end
  ask_note = keep_ask(ui.label(page_w - 2 * kMargin, ui.line_height(kSmall) + 6))
  if ask_note ~= nil then
    ask_note.text = (kind == 'export')
                          and 'a PNG of eight bits of grey, which any program reads'
                          or 'a drawing of four bits, the cheapest picture of this system'
    ask_note.scale = kSmall
    ask_note.align = 'left'
    ask_note.background = 15
  end
  local width = page_w - 2 * kMargin - 2 * kAskPad
  local half = (width - 24) // 2
  ask_cancel = keep_ask(ui.button(half, ui.line_height(kBody) + 14))
  if ask_cancel ~= nil then
    ask_cancel.title = 'Cancel'
    ask_cancel.scale = kBody
    ask_cancel.on_click = function() ask_wish = 'cancel' end
  end
  ask_ok = keep_ask(ui.button(half, ui.line_height(kBody) + 14))
  if ask_ok ~= nil then
    ask_ok.title = (kind == 'export') and 'Export' or (kind == 'save_as') and 'Save as' or 'Save'
    ask_ok.scale = kBody
    ask_ok.primary = true
    ask_ok.on_click = function() confirm_the_ask() end
  end
  layout_the_ask()
  if keyboard == nil then keyboard = ui.keyboard() end
  if keyboard ~= nil and ask_field ~= nil then
    keyboard.target = ask_field
    keyboard:show('alpha')
  end
  ui.focus(ask_field)
  ui.present('gc16')
end

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
    size_key.max = kThickest
    size_key.step = 1
    size_key.value = now.size
    size_key.suffix = ' px'
    size_key.scale = kSmall
    size_key.on_change = function(self, value)
      now.size = math.max(1, math.min(kThickest, math.floor(tonumber(value) or now.size)))
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
        -- **`New` i `Undo` són les dues portes que faltaven** (0.192.56, l'amo: *«Al Menú contextual de Paint li falta File->New,
        -- també Edit -> Undo (per desfer l'últim traç)»*). `New` no llença res: the drawing that is being left goes to its file
        -- first, and what comes back is a white paper without a name (docs/30, docs/90).
        { label = 'New' },
        { label = 'Save' },
        { label = 'Save as' },
        { label = 'Export' },
        '-',
        { label = 'Close' },
      } },
      { title = 'Edit', items = {
        { label = 'Undo' },
      } },
    }
    bar.on_pick = function(self, title, entry)
      if title == 2 then
        -- **El menú no dibuixa: diu què vol i marxa** (0.192.51): the paper is put back and painted outside this hook, like every
        -- other job of this page (docs/30, docs/90).
        if entry == 1 then
          do_this = { kind = 'undo' }
          ui.present(kCheapMode)
        end
        return
      end
      if title ~= 1 then return end
      if entry == 1 then
        do_this = { kind = 'new' }
        ui.present(kCheapMode)
      elseif entry == 2 then
        -- **El menú no desa: diu què vol i marxa** (0.192.51, l'amo: *«Save no mostra una vista per guardar amb nom»*). El que es fa
        -- aquí és dir-ho i marxar, perquè la pàgina del dibuix s'ha de tornar per fer-hi lloc i un control no es torna dins del seu
        -- propi ganxo (docs/30, docs/90). **`Save` desa amb el nom que el dibuix ja té** i només demana quan no en té (0.192.53).
        wanting = 'save'
        ui.present(kCheapMode)
      elseif entry == 3 then
        -- **`Save as` és per a un altre nom** (0.192.53, l'amo: *«Afegeix també "Save as" per si l'usuari vol guardar el dibuix amb
        -- unaltre nom»*): sempre pregunta, i ho fa amb el nom d'ara al camp, que és el que es canvia.
        wanting = 'save_as'
        ui.present(kCheapMode)
      elseif entry == 4 then
        -- **I exportar sempre pregunta** (0.192.53, l'amo: *«A exportar sempre pregunt nom, només suggereix el mateix nom que s'ha
        -- guardat»*): el que hi ha al camp és el nom del dibuix, i l'extensió del PNG la posa l'app.
        wanting = 'export'
        ui.present(kCheapMode)
      elseif entry == 6 then
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
    forget_the_undo()  -- the step back was a capture of the paper of another size
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
paint_the_frame = function()
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
-- that pauses or closes keeps its drawing in the file it came from —or in a draft (`/data/paint/drawings/draft.ics`), when nobody
-- has named it yet—, and the name of that file is remembered in `status` so that the next start opens **the same drawing** and not
-- the draft of the last one that had no name (0.192.56, docs/30, docs/75).
local opened = false
if app.args ~= nil and app.args.path ~= nil and app.args.path ~= '' then
  local what, why = open_the_drawing(app.args.path)
  opened = what ~= nil
  if what == nil then say_the_note(tostring(why or 'the drawing did not open')) end
end
if not opened then
  local remembered = the_drawing_remembered()
  if remembered ~= '' and fs.stat(remembered) ~= nil then
    local what, why = open_the_drawing(remembered)
    opened = what ~= nil
    if what == nil then say_the_note(tostring(why or 'the drawing did not open')) end
  end
end
if not opened and folder ~= nil then
  local draft = folder .. '/draft.' .. kExtension
  if fs.stat(draft) ~= nil then
    local what, why = open_the_drawing(draft, false)
    if what ~= nil then
      opened = true
    else
      say_the_note(tostring(why or 'the draft did not open'))
    end
  end
end
-- **La pàgina diu quin dibuix té** (0.192.56): a drawing that comes back without a word is a page that nobody can tell from a fresh
-- one, i el nom el recorda `status` i no el camí d'aquest arrencada (docs/30, docs/75).
console.print('paint: ' .. ((now.file ~= '') and ('the drawing is ' .. now.file) or 'a drawing with no name yet'))
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

  -- **El que el menú ha demanat es fa aquí** (0.192.51): obrir la pàgina del nom vol tornar la del dibuix, i això no es fa dins del
  -- ganxo del menú; i la feina que ha demanat la pàgina del nom —desar o exportar— també és del bucle, perquè el que ha de dir el
  -- peu i el que ha de tornar a la pàgina són seves i no del ganxo d'un botó (docs/30, docs/90).
  if wanting ~= nil and asking == nil then
    local kind = wanting
    wanting = nil
    -- **`Save` amb un nom ja posat no pregunta res** (0.192.53): el dibuix que té fitxer es desa a sobre i prou, que és el que vol
    -- dir desar; per a un altre nom hi ha `Save as`, i `Export` sempre pregunta i suggereix el nom que el dibuix ja té, perquè el que
    -- canvia és només l'extensió (l'amo: *«A exportar sempre pregunt nom, només suggereix el mateix nom que s'ha guardat»*).
    if kind == 'save' and now.file ~= '' then
      do_this = { kind = 'save', path = now.file }
    else
      open_the_ask(kind)
    end
  end
  if ask_wish ~= nil and asking ~= nil then
    ask_wish = nil
    close_the_ask()
  end
  if do_this ~= nil then
    local job = do_this
    do_this = nil
    local what, why
    if job.kind == 'undo' then
      -- **Un traç que encara no s'ha pintat es desfà sol** (0.192.56): it is the last thing the hand did, it is still a list of
      -- points, and there is nothing on the paper to give back —so the step back is free and needs no block at all (docs/30).
      if #strokes > 0 then
        strokes = {}
        say_the_note('the last stroke was taken back')
      elseif undo_the_paper() then
        say_the_note('the last stroke was taken back')
      else
        say_the_note('nothing to undo')
      end
    elseif job.kind == 'new' then
      -- **`New` no llença el dibuix a les escombraries** (0.192.56): it goes to its file first —the same door `Close` uses— and what
      -- the page gets is a white paper with no name, o sigui que el `Save` de després tornarà a preguntar on va (docs/30, docs/75).
      keep_the_drawing()
      if paper ~= nil then
        paper:clear(15, kPaperMode)
        paper:invalidate()
      end
      strokes = {}
      forget_the_undo()
      now.file = ''
      remember_the_drawing()
      save_state()
      say_the_note('a new drawing')
    elseif job.kind == 'export' then
      -- **Exportar desa també el dibuix** (0.192.51): a PNG is a copy for another program, i el que aquesta app té és el `.ics`; un
      -- export que deixés el dibuix sense desar seria una còpia d'una cosa que ningú no té (docs/46).
      local kept = save_the_paper(with_another_extension(job.path, kExtension))
      if kept == nil then
        what, why = nil, 'the drawing did not save, so it was not exported'
      else
        what, why = export_the_paper(job.path)
      end
    else
      what, why = save_the_paper(job.path)
    end
    if job.kind == 'export' or job.kind == 'save' then
      if what == nil then
        say_the_note(tostring(why or 'that did not work'))
      elseif job.kind == 'export' then
        remember_the_drawing()
        say_the_note('exported ' .. the_name_only(what))
      else
        remember_the_drawing()
        say_the_note('saved ' .. the_name_only(what))
      end
    end
  end

  -- **Mentre la targeta d'un menú és a sobre, el dit és seu** (docs/30): the card of the bar hangs over the top of the paper, and a
  -- finger on an entry —`Close` is the third line, and it is over the drawing— would be a finger on the drawing too: the stroke would
  -- be drawn under the card a person is reading. The gesture is taken here and not done, because a queue nobody empties grows.
  -- **I mentres la pàgina del nom és al vidre tampoc no es dibuixa**: el full blanc tapa el paper i un dit sobre el camp del nom no
  -- pot deixar un traç a sota (0.192.51).
  local ask_up = asking ~= nil or (sys.time_ms() - ask_closed_at) < 500
  local menu_up = open > 0 or just_changed or ask_up
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
      end
      -- **Un toc sol no dibuixa res** (0.192.51): the gesture is the palette's, and a tap on the paper used to leave a dot —which is
      -- also what made a finger that pressed a key of the ask page leave one under it, because the tap of a control and the tap of
      -- the page are the same gesture (vist a l'emulador). El que dibuixa és el traç, i un traç és un dit que es mou (docs/30).
    end
    if #fingers > 0 then
      local finger = fingers[1]
      if in_the_paper(finger.x, finger.y) then
        on_the_paper = true
        if not drawing then
          drawing = true
          start_a_stroke(finger.x - paper_x, finger.y - paper_y, stamp)
        end
        add_a_point(finger.x - paper_x, finger.y - paper_y, stamp)
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

-- **El teclat canvia l'alçada de la pàgina** (0.192.51): the band of the keyboard takes the bottom of the view and the system says
-- so with `on_resize`; mentres la pàgina del nom és al vidre el que es torna a col·locar és ella —el camp, els botons i el full— i
-- el dibuix no es toca (docs/30, docs/90).
function on_resize()
  if asking ~= nil then
    layout_the_ask()
    if ask_sheet ~= nil then ask_sheet:invalidate() end
    ui.present('gc16')
    return
  end
  layout()
  paint_the_frame()
  if paper ~= nil then paper:invalidate() end
  if palette ~= nil then palette:invalidate() end
  ui.present(kPaperMode)
end

function on_pause()
  -- **Un dibuix a mig fer no es perd per obrir la pàgina del nom**: while the ask is up the paper is still the page's and a pause
  -- has nothing to keep —the drawing is already where it was (docs/30, docs/75).
  if asking == nil then keep_the_drawing() end
  save_state()
  -- **El pas enrere no marxa amb la pàgina** (0.192.56): a scratch of the size of the paper that is kept is parked to the card on
  -- every pause —half a second of writing each time an app comes to the glass—, so the block is given back here and the step back
  -- is a thing of the page that is on the glass (docs/30, docs/75).
  forget_the_undo()
end

function on_save()
  if asking == nil then keep_the_drawing() end
  save_state()
end

function on_close()
  if asking == nil then keep_the_drawing() end
  save_state()
end

-- **Una pàgina que marxa deixa paper net**: the controls go first, because a bar that still owes pixels paints itself over the wipe
-- (docs/30, docs/90). **I el bloc del pas enrere es llença abans**, que un bloc de la sessió que viu és mitja pàgina escrita a la
-- targeta quan l'app s'acaba (0.192.56, docs/75).
forget_the_undo()
local kids = ui.root().children
for i = #kids, 1, -1 do kids[i]:release() end
ui.clear(15)
ui.present('gld16')
console.print('paint: closed')
