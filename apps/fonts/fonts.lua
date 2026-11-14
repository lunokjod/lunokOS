-- fonts: **the faces this board can read, drawn with their own letters** (0.174.1). The door `/fonts` is three places in
-- one —the flash's `res/fonts/` (the system's faces), the card's `fonts/` and, for an app that carries them,
-- `apps/<id>/fonts/`— and this page is where a person sees what is there (docs/30, docs/70).
--
-- **IT OPENS EVERY FORMAT THE SYSTEM KNOWS AND NOT ONLY THE ONE IT EXPECTS**: the four readers of this kernel are a
-- `.lfont` (its portable bitmap face), a `.bdf`, a `.pcf` and a TrueType of outlines, and the shelf asks the system to
-- load each file once and says which one it is and what it holds. A file that is not a face is not hidden: it says the
-- sentence the reader gave, which is the difference between "my font does not appear" and "my font is not a font"
-- (docs/30, docs/92).
--
-- **THE FORMAT IS READ FROM THE FILE AND NOT FROM THE NAME**: `ui.load_font` looks at what the file says of itself, so
-- an extension is a habit and not a contract. This page sniffs the same four signatures —a TrueType table directory,
-- `STARTFONT`, `lunok-font` and the four bytes of a PCF— and it is what names the format on the glass (docs/30).
--
-- **IT IS NOT AN APP OF PROVA**: it carries the icon of a normal app, because it is one a person uses to know what
-- their board is drawing with (docs/40).
--
-- **I cada fila del prestatge diu el que un fitxer és amb dues tintes** (0.189.3, l'amo: *«el nom de la font en gran i a
-- sota els detalls, el tipus i el tamany i el nom del fitxer; cada entrada tindrà dos colors B/N amb diferents tamanys de
-- lletra»*): the name —the file's, without the extension it happens to carry— in a band of white on black, and under it
-- the details of the face, its type with the bytes, and the file's own name in black on white. The name of a file is
-- `fs.list`'s and not a guess: a face that the loader opens does not carry it anywhere (docs/30, docs/70).
--
-- **I EL TÍTOL DE LA FILA ÉS LA MOSTRA DE LA CARA, GUARDADA EN UNA SESSIÓ** (0.189.26, l'amo: *«l'app Fonts es una galeria
-- de tipus de lletra i guarda una mostra del tipus de lletra amb el nom de la font … a una sessió de sistema. Quan s'obre
-- l'app Fonts es busquen aquestes sessions de sistema per recuperar la previsualització de la font, que s'usarà com a títol
-- de l'entry corresponent a la llista de fonts»*): the name of the face **drawn with that face** —«DejaVu Sans» in DejaVu
-- Sans— and kept as a picture in a block of the session store (`sys.session`, `cv:capture`), so the page draws it again as
-- a copy (`cv:blit`) and the face does not have to be alive to show it: this board keeps three faces at a time and a shelf
-- of nine is not one of the three (docs/30, docs/75). The block is asked first and the face only when there is nothing to
-- copy, and a file that changed (another size the last time) is drawn again.
--
-- **I LA PÀGINA D'UNA CARA ÉS UN EXEMPLAR AMB UNA PORTA ABAIX DE TOT** (0.189.31, l'amo: *«A la vista detalls: Tot el text
-- mes gran. El botó de "back to the shelf" abaix de tot amb una icona de "back" a l'esquerra i el text a la dreta. Entre els
-- detalls i el botó un TextBoxMultiline sense vores i read only ple de text, en un passatge del llibre "The Time Machine.epub"
-- d'H.G. Wells»*): the face's name and what the system made of it at `kSpecHead` and `kSpecNote`, the specimen in its own
-- letters, **five passages of Wells in a box of lines with no frame that is read and not written**, and the way back with the
-- arrow of the system on its left (docs/30, docs/90).

local caps = sys.caps()
if not caps.display then
  console.print('this board needs a glass for this')
  return
end

-- **The quality of a page is the system's** (0.179.5): this app builds its pages and presents them with `ui.present()` and
-- no quality of its own, because a page of words is not a photograph and the one that knows what the glass had before it is
-- the kernel, which counts its inks. It used to be a `kPageMode = 'gc16'` here: four flashes for a page of text on paper the
-- system had already left white (docs/30, docs/50).

-- The sizes of the page, in pixels of line: the face turns them into steps, and a step is eleven pixels with the bold
-- face of the disk and seven with the five by seven of the flash (docs/30, docs/92).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

-- **The label is measured and not guessed**: a sentence that does not fit is a sentence that ends in "..." and says
-- nothing, which is what the head of this page said the first time it was looked at (docs/30, docs/92).
local function fit(text, width, biggest)
  local scale = biggest
  while scale > 1 and ui.text_width(text, scale) > width do
    scale = scale - 1
  end
  return scale
end

-- **The same measure with a face that is not the system's**: a specimen is drawn with the face it shows, so the page
-- asks that face how wide the row is and not the one of its own labels (docs/30).
local function fit_face(face, text, width, biggest)
  local scale = biggest
  while scale > 1 and face:text_width(text, scale) > width do
    scale = scale - 1
  end
  return scale
end

local kSmall = scale_for(20)
local kBody = scale_for(30)
local kBig = scale_for(46)
--  **El títol d'una fila i el text de sota, més grans** (0.189.30, l'amo: *«Fes mes gran el títol del tipus de lletra i posa
--  fons blanc, a la part d'abaix fes-la gris molt claret de fons. i un tipus de lletra lleugerament mes gran»*, i una altra
--  empenta a la 0.189.32: *«Fes mes gran tot el text, sobretot el títol»*): the title is the sample of the face and it is the
--  thing the page is for, so it is drawn at `kTitle` —six steps, sixty-six pixels, a half more than the forty-four of
--  before— and the three lines of the details at `kDetail`, which is three steps and not two; both are the rows of the
--  layout and `kRow` follows them (docs/30, docs/92).
local kTitle = scale_for(66)
local kDetail = scale_for(36)
--  **Les dues tintes d'una fila**: the title on **white paper** and the details on a **very light grey** one, so the two
--  halves of a row are read as two things without a line between them (the owner's ask; the greys of this glass are
--  sixteen and 13 is the light one that still reads as paper and not as the page) (docs/30, docs/92).
local kTitlePaper = 15
local kDetailPaper = 13
--  **El text de la pàgina de detall, dos passos més gran** (0.189.31 i 0.189.32, the owner: *«A la vista detalls: Tot el
--  text mes gran»* i *«Fes mes gran tot el text, sobretot el títol»*): the name of the face goes from `kBig` to `kSpecHead`
--  —six steps, the same size as the title of a row— and the sentences under it from `kSmall` to `kSpecNote`, which is four
--  steps; `fit` still cuts a long name back down, so nothing is drawn wider than the page (docs/30, docs/92).
local kSpecHead = kBig + 2
local kSpecNote = kSmall + 2
--  **I la porta de tornar té una mida seva i és petita** (0.189.32 i 0.189.33, l'amo: *«Fes el botó mes petit i discret,
--  que no ocupi tot l'ample, alineat a l'esquerra»*): it was the height of a row of the shelf —a number of the shelf, which
--  grows with the title— and the width of the whole column. `kDoorW` by `kDoorH` is a door of 200 by 72: the mark keeps a
--  square of the height (68, which is the sixty-four of the picture at its own size and not blown up) and the word has the
--  rest —`Back`, which is what a door of four letters fits— (docs/30, docs/92).
local kDoorW = 200
local kDoorH = 72
--  **I el swipe de tornar té una distància** (0.189.35, l'amo: *«A la vista detall registra el swipe a l'esquerra per fer
--  "back"»*): the system has no `swipe` kind —a finger that slides is a `drag` with an `dx` and a `dy` (docs/30)— and the
--  drag that goes back is the one that walked sideways more than this many pixels with more sideways than up or down.
--  Sixty-four is the width of the mark of a door: less than that is a finger resting on the glass. **The side does not
--  matter** (0.189.37, the owner: *«com el faig cap a la dreta?»*), and the drags of the shelf are not a page turn either
--  (docs/30, docs/92).
local kBackSlop = 64
-- **The same text in four sizes**, which is what a specimen is: a face of outlines is drawn at each of them and a bitmap
-- face is multiplied, and a person sees that with one look (docs/30).
local kSample = { 7, 4, 3, 2 }
-- **Els passatges d'un llibre que hi ha al disc d'aquesta placa** (0.189.31, the owner: *«Entre els detalls i el botó un
-- TextBoxMultiline sense vores i read only ple de text, en un passatge del llibre "The Time Machine.epub" d'H.G. Wells.
-- Agafa varis passatges d'unes quantes línies per usar-lo d'exemple»*): five passages of *The Time Machine* (1895, public
-- domain), **a paragraph to a line and the box wrapping them** so the measure of the page and not of this file decides
-- where a line ends, and written **ascii** —the curly quotes and the long dashes of the book are two bytes here, and the
-- text of this system is one byte a character— (docs/30, docs/90).
local kPassages =
  'The Time Machine, by H. G. Wells\n' ..
  '\n' ..
  'The Time Traveller (for so it will be convenient to speak of him) was expounding a recondite matter to us. His ' ..
  'grey eyes shone and twinkled, and his usually pale face was flushed and animated. The fire burnt brightly, and ' ..
  'the soft radiance of the incandescent lights in the lilies of silver caught the bubbles that flashed and passed ' ..
  'in our glasses.\n' ..
  '\n' ..
  'I shall have to controvert one or two ideas that are almost universally accepted. The geometry, for instance, ' ..
  'they taught you at school is founded on a misconception.\n' ..
  '\n' ..
  'I am afraid I cannot convey the peculiar sensations of time travelling. They are excessively unpleasant.\n' ..
  '\n' ..
  'Nature never appeals to intelligence until habit and instinct are useless. There is no intelligence where there ' ..
  'is no change and no need of change.\n' ..
  '\n' ..
  'And I have by me, for my comfort, two strange white flowers, shrivelled now, and brown and flat and brittle, to ' ..
  'witness that even when mind and strength had gone, gratitude and a mutual tenderness still lived on in the ' ..
  'heart of man.\n'

local view = ui.caps()
local margin = 0
local kRow = 0
local inner = 0

local function build_layout()
  view = ui.caps()
  margin = math.max(10, view.width // 24)
  inner = view.width - 2 * margin
  --  **Una fila porta el títol i tres línies de detalls** (0.189.3, i les mides de la 0.189.30): the band of the sample and
  --  the three sentences under it, measured from what they need and never under a fingertip —a row of this page is read at
  --  a glance and pressed with the same finger— (docs/30, docs/90).
  local needed = ui.line_height(kTitle) + 10 + 2 + 3 * ui.line_height(kDetail) + 4
  kRow = math.max(math.max(72, needed), math.floor(view.height * 0.13))
end

-- **What the format of a file is**, from the bytes and not from the name: the four signatures the kernel's loader looks
-- at, in the same order, so this page and `ui.load_font` can never say different things (docs/30).
local function sniff(path)
  local head = fs.read_at(path, 0, 256)
  if head == nil or head == '' then return nil, 'the file cannot be read' end
  local b0, b1, b2, b3 = head:byte(1, 4)
  if b0 == nil then return nil, 'the file is empty' end
  if b0 == 0x00 and b1 == 0x01 and b2 == 0x00 and b3 == 0x00 then return 'TrueType' end
  if head:sub(1, 4) == 'true' then return 'TrueType' end
  if head:sub(1, 4) == 'OTTO' then return 'OpenType' end
  if head:sub(1, 4) == '\1fcp' then return 'PCF' end
  -- **The first line that says something**, skipping the blank ones and the comments of the two text formats, which is
  -- exactly the walk of `font::load_any` (docs/30).
  for line in head:gmatch('([^\r\n]*)[\r\n]') do
    local trimmed = line:gsub('^%s+', '')
    if trimmed ~= '' and trimmed:sub(1, 1) ~= ';' and trimmed:sub(1, 7) ~= 'COMMENT' then
      if trimmed:sub(1, 9) == 'STARTFONT' then return 'BDF' end
      if trimmed:sub(1, 10) == 'lunok-font' then return 'lunokOS' end
      break
    end
  end
  return nil, 'not a face'
end

-- **A file asked to the system**: what it is, and what it holds. The face is loaded and given back at once, because this
-- board keeps three faces at a time and a shelf of eight of them is not one of the three (docs/30, docs/75).
local function probe(path)
  local kind, why = sniff(path)
  if kind == nil then return nil, why end
  local face, err = ui.load_font(path)
  if face == nil then
    return nil, (err and err.message) or (err and err.code) or 'the face cannot be opened'
  end
  local width, height, glyphs = face:size()
  face:release()
  return kind, nil, width, height, glyphs
end

-- **LA BARRA DE L'ARRENCADA, SOBRE LA TARGETA DE L'APP** (0.189.33, l'amo: *«Afegeix una progressbar al centre de la pantalla
-- mentre hi sigui l'splash. Aquesta barra només l'has de mostrar quan l'app arrenca (abans de dibuixar res)»*): this page has
-- to ask the system about **every file of the door** before it can draw a single row, and each of those is a face opened and
-- given back —the slowest thing this app does—. While that runs the glass has the app's own title card, which the kernel
-- painted before the script existed.
--
-- **I la barra hi cap sense esborrar la targeta** (0.189.38, l'amo: *«Fes que al arrencar l'app no refresqui la seva vista
-- (no comunicant damage al panell?) a la pantalla eink, així es queda l'splash amb la barra de progrés que creix mentre
-- l'app carrega completament … Un cop acabat, amaguem la barra de progrés i refresquem la vista sencera»*): the picture of
-- the manifest is on the glass and the system **owes the whole page**, and that debt is what takes the picture away —a
-- `ui.present()` here painted the white page over the splash at the first step of the bar—. `ui.present_over()` is the
-- present of a start: the box of the bar goes up **over the picture** and the debt stays, so the bar grows on top of the
-- splash; the whole page is paid by the `ui.present()` of `build_shelf`, which is the page of the app complete (docs/30).
--  **LA BARRA D'UN ARRENCAMENT ÉS UNA LÍNIA, I UNA LÍNIA I PROU** (0.189.40 i 0.189.41, l'amo: *«Fes la barra mes fina,
--  pósala mes avall, sota del "loading" i assota»* i, quan va veure el nom que hi havia sota, *«el text queda horrible.
--  Treu-lo. Amb la barra serà suficient»*): the bar was `line_height(kSmall)` of body —twenty-two pixels and a plate— and
--  it sat in the middle of the page, which over the picture of `res/splash.png` is **on top of the word «Fonts»**. The
--  numbers below are **glass** rows of that picture, measured band by band on the dump of a run: the icon ends at 433, the
--  rule is at 501, «Fonts» runs 529..560, «loading...» 594..609 and the watermark «lunokOS» starts at **669**. The bar goes
--  in the sixty pixels between the sentence and the watermark —**nine of ink and no name**, because a line of text under a
--  line of ink was the name of a file at the size of a label and the owner saw what it was—, and **the place is said on the
--  glass and not in the view**: a control is placed where the app's page is (the view starts at the row of the bar of the
--  system, forty-eight of them), so a number written as if it were the view put the bar **on top of the watermark** the
--  first time this was tried (docs/30, docs/50).
--
--  **Nou i no sis**: nine pixels is the thinnest a bar of this system is, because the frame is one pixel and it is only
--  drawn above eight —a bar of six is a track with no edge, which on paper is a smear and not a line (docs/90).
local kBarGlassY = 636
local kBarH = 9

--  **El lloc de l'arrencada, dit en veu alta quan l'app és la del prompt** (0.189.40, docs/85): `run fonts shelf` prints the
--  box of the bar in **glass** rows, so the case that has no eyes can measure that it is thin and that it is under the
--  «loading...» of the picture and above its watermark (docs/30).
local start_boxes = nil

local function raise_the_bar()
  local width = math.min(inner, 320)
  if width % 2 ~= 0 then width = width - 1 end
  local bar = ui.progress(width, kBarH)
  if bar == nil then
    console.print('fonts: no bar for the start')
    return nil
  end
  --  **Els tons de la casa**: the frame of ink, the paper of the page and the light tone of the project for what is done,
  --  which is the drawing of every bar of this system (docs/92).
  bar.value = 0
  local left = (view.width - width) // 2
  --  **On el vidre i no on la vista**: `view.y` is the row of the system's bar, which is above the page of the app (docs/30).
  bar:place(left, kBarGlassY - view.y)
  start_boxes = { x = left, y = kBarGlassY, w = width, h = kBarH }
  --  **La barra i prou**: the page is still owed, which is the splash of this app (0.189.38, docs/30).
  ui.present_over()
  return { bar = bar }
end

-- **La barra camina poques vegades**: a paint of this glass is a tenth of a second and the door is read in one go, so the
-- number moves **eight times at most** and never for nothing —a bar that costs as much as the work it reports is a page that
-- got slower to say it was busy— (docs/50).
local kBarSteps = 8

--  **I el nom del que s'està llegint només va al registre** (0.189.41, l'amo: *«el text queda horrible. Treu-lo. Amb la
--  barra serà suficient»*): the label under the bar is gone —a line of text at the size of the page under a line of ink was
--  not a thing a person wants to look at— and the name of the family being read is said **in the console**, which is where
--  a start that stops half way is explained without a window in front of it (docs/30, docs/85). It is said with the step of
--  the bar, which is eight lines and not seventeen.
local function bar_says(stage, done, total, name)
  if stage == nil or total <= 0 then return end
  if stage.bar == nil then return end
  local step = 1
  if total > kBarSteps then step = math.ceil(total / kBarSteps) end
  if done % step ~= 0 and done ~= total then return end
  stage.bar.value = math.floor(100 * done / total)
  if name ~= nil then console.print('fonts: loading ' .. name) end
  --  **I cada passa és una caixa sobre el splash** (0.189.38): the box of the bar and not the page, which is owed (docs/30).
  ui.present_over()
end

local function drop_the_bar(stage)
  if stage == nil then return end
  if stage.bar ~= nil then stage.bar:release() end
  console.print('fonts: the bar of the start is gone')
end

-- **The faces of the door, by name, with what each one is**. The bytes are the file's and not a guess: `fs.list` gives them
-- and the shelf shows them, because a face of two kilobytes and one of twenty are two different things to a person
-- (docs/30).
--
-- **I UN FITXER QUE NO ÉS UNA CARA NO ÉS UNA FILA** (0.189.32, l'amo: *«Treu els arxius que no son fonts reconegudes de la
-- llista»*): the door is a folder of the card and of the flash, and half of what a person drops there is a licence, a
-- readme or a face of a format this kernel does not read. A row of it was a grey row that could not be pressed and took
-- the room of a face, so it is **not listed** —the page is a shelf of faces— and **the reason is written to the console one
-- line per file** (`fonts: LICENCE.txt none 8 147 not a face`), which is where the question «my font does not appear» is
-- answered without a row for it (docs/30).
local files = {}
local left_out = 0

-- **I la barra de l'aprenentatge, si algú l'ha aixecada** (0.189.33): `stage` is the bar of the start and the name under
-- it, or nil; the walk says how far it has got every few files and whose name it is reading (0.189.39). A call from the
-- shelf rebuilding itself passes nothing and pays nothing.
local function read_the_shelf(stage)
  local entries = fs.list('/fonts')
  files = {}
  left_out = 0
  if entries == nil then
    console.print('fonts: /fonts cannot be listed')
    return
  end
  for i = 1, #entries do
    local one = entries[i]
    if one.type == 'file' then
      local kind, why, width, height, glyphs = probe('/fonts/' .. one.name)
      local size = one.size or 0
      if kind == nil then
        left_out = left_out + 1
        console.print(string.format('fonts: %s none %d %s', one.name, size, tostring(why)))
      else
        files[#files + 1] = {
          name = one.name,
          path = '/fonts/' .. one.name,
          size = size,
          kind = kind,
          why = why,
          width = width or 0,
          height = height or 0,
          glyphs = glyphs or 0,
        }
        console.print(string.format('fonts: %s %s %d', one.name, kind, size))
      end
      --  **El nom del que s'està llegint, cap al registre** (0.189.41): the family of a file without the extension it happens
      --  to carry, which is the word the row of the shelf will show, and only when the file is a face —a licence is not the
      --  name of what a bar is loading— (docs/30, docs/85).
      local stem = one.name:match('^(.*)%.[^%.]+$') or one.name
      bar_says(stage, i, #entries, (kind ~= nil) and stem or nil)
    end
  end
  table.sort(files, function(a, b) return a.name:lower() < b.name:lower() end)
end

-- A number of bytes the way a person reads it, which is what the detail of a row says: the three steps of the glass.
local function bytes_text(size)
  if size >= 1024 * 1024 then
    return string.format('%.1f MB', size / (1024 * 1024))
  end
  if size >= 1024 then
    return string.format('%.1f kB', size / 1024)
  end
  return string.format('%d B', size)
end

-- **El nom de la font, tal com una persona la nomena** (0.189.3): the file's name without the extension it happens to
-- carry, because this system reads the format from the bytes and not from the name —a `.ttf` that is a BDF is a BDF here
-- (docs/30)—. **The file's own name is a line of the row**, so the two are not the same thing and neither is guessed.
local function family_of(record)
  local stem = record.name:match('^(.*)%.[^%.]+$')
  return stem or record.name
end

-- **El tipus, en una paraula**: the extension the file carries, and what the reader saw inside it when the name has none.
local function type_of(record)
  local ext = record.name:match('%.([%w]+)$')
  if ext ~= nil then return ext:lower() end
  return record.kind or 'file'
end


-- **What a face holds**, which is not the same thing for the two families: a table of bits has a cell and a count of
-- codes, and a face of outlines has neither —its glyphs are contours in the file and it is drawn at every size— (docs/30).
local function shape_of(record)
  -- **A face of outlines has no cell**: `width` is zero and every glyph is drawn from its contours at the size the text
  -- asks, which is the whole difference between the two families (docs/30).
  if record.width == 0 then
    return string.format('outlines, %d codes, a line of %d px per step', record.glyphs, record.height)
  end
  return string.format('%dx%d, %d glyphs', record.width, record.height, record.glyphs)
end

-- **Els detalls d'una font en l'amplada d'una línia** (0.189.3): the same two families said short, because a row of the
-- shelf carries them under the name and a sentence that ends in "..." says nothing (docs/30, docs/92).
local function shape_short(record)
  if record.kind == nil then return record.why or 'not a face' end
  if record.width == 0 then return string.format('outlines, %d codes', record.glyphs) end
  return string.format('%dx%d a cell, %d glyphs', record.width, record.height, record.glyphs)
end

-- --------------------------------------------------------------------- la forma de la vista

-- **La forma d'aquesta pàgina, en la seva sessió `state`** (0.189.27, la norma 0.189.12 de docs/30): el bloc i el parell
-- que el desa i el llegeix **es copien sencers del contracte**, perquè «quin control tenia el cercle» és la mateixa
-- pregunta a totes les apps i una app que ho fa a la seva manera és una app on el format canvia sense que ningú ho vegi
-- (docs/30, docs/45).
--
-- **Què hi ha a dins i per què**: `page` és la pàgina del prestatge (una llista que en té cinc i la persona n'era a la
-- tercera), `focus` és la fila que tenia el cercle —pel nom del fitxer, que és el nom que aquesta app dóna a una fila— i
-- `marked` és la fila **que té el seu exemplar obert**: el prestatge pot tenir el cercle en una fila i una altra oberta, i
-- són dues coses que una persona trobaria a faltar separades (docs/30).
local kStateBytes = 1024
local kLayout = 'v1'   -- v1: page, focus and marked

local state = nil      -- the block: asked once and kept for the life of the app
local function the_state()
  if state == nil then
    local block = sys.session('state', kStateBytes)
    if block == nil then return nil end
    state = block
  end
  return state
end

local function escape(text)
  return (tostring(text or ''):gsub('\\', '\\\\'):gsub('\n', '\\n'))
end

local function unescape(text)
  -- **A Lua pattern does not escape with a backslash** —`%` is its escape— so one `\\` in the literal is one
  -- backslash in the text, and `\\(.)` would be asking for two of them (found by running this very snippet, 0.189.12).
  return (text:gsub('\\(.)', function(c) return (c == 'n') and '\n' or c end))
end

-- **Què és la pàgina ara**, una línia per cosa: la forma, la pàgina, el cercle i la fila oberta.
local function save_state(now)
  local block = the_state()
  if block == nil then return false end
  local lines = { kLayout }
  for _, key in ipairs({ 'focus', 'page', 'tab', 'scroll', 'marked' }) do
    if now[key] ~= nil then lines[#lines + 1] = key .. '=' .. tostring(now[key]) end
  end
  for name, value in pairs(now.check or {}) do
    lines[#lines + 1] = 'check:' .. name .. '=' .. (value and '1' or '0')
  end
  for name, value in pairs(now.text or {}) do
    lines[#lines + 1] = 'text:' .. name .. '=' .. escape(value)
  end
  block:clear()
  block:write(0, table.concat(lines, '\n'))
  return true
end

-- **Què era la pàgina**: `nil` quan no hi ha res a llegir, o quan el bloc és d'una altra forma —un estado que no és
-- d'aquesta pàgina és pitjor que cap estat, perquè posaria el cercle en un control que ja no hi és.
local function load_state()
  local block = the_state()
  if block == nil then return nil end
  local body = block:read(0, kStateBytes) or ''
  local now = { check = {}, text = {} }
  local first = true
  for line in body:gmatch('[^\n]+') do
    if first then
      first = false
      if line ~= kLayout then return nil end
    elseif line:sub(1, 6) == 'check:' then
      local name, value = line:match('^check:([^=]+)=(%d)$')
      if name ~= nil then now.check[name] = (value == '1') end
    elseif line:sub(1, 5) == 'text:' then
      local name, value = line:match('^text:([^=]+)=(.*)$')
      if name ~= nil then now.text[name] = unescape(value) end
    else
      local key, value = line:match('^([%w_]+)=(.*)$')
      if key ~= nil then now[key] = value end
    end
  end
  if first then return nil end
  return now
end

-- **La forma que la pàgina té ara**, que és el que es desa: es toca a mesura que la persona es mou i no es torna a
-- preguntar al sistema enlloc, perquè el que la vista és ho sap aquesta pàgina i ningú més (docs/30).
local shape = { page = 1, focus = nil, marked = nil }

-- --------------------------------------------------------------------- the page

-- **The controls of what is on the glass are given back together**: a page of this app is a panel of labels, and a label
-- that is not released keeps its slot and its draw. `release` marks it and the system takes the block at the end of the
-- present that paints over it (docs/30, docs/90).
local screen = nil
local shelf_list = nil
local shelf_rows = {}
-- **The places of the shelf, said out loud when the app is asked to**: `run fonts shelf` prints the centre of every row
-- and the box of the way back, so a test presses them without knowing the layout (docs/30, docs/85).
local debug_boxes = (app.args ~= nil and app.args.path == 'shelf')
-- **The three pages of this app ask for each other**, so they are named before they are written: a shelf that opens a
-- specimen and a specimen that goes back to the shelf is a pair and not a tree (docs/30).
local build_shelf
local open_specimen
local go_back

-- **The face of a specimen belongs to the page and not to the moment** (0.177.1): a label keeps the **number** of the face
-- it draws with and not the object, so a face that the collector of Lua takes away leaves every label of the page drawing
-- with the face of the system without saying a word —and a page that opens four faces asks the fifth for a slot this board
-- does not have, because `kMaxFaces` is three—. It is kept here while the specimen is on the glass and it is given back
-- **after** the controls that draw with it, because `face_of()` answers the face of the system for a slot that is gone
-- (docs/30, docs/75).
local specimen_face = nil

local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
  shelf_list = nil
  -- **The controls first and the face after**: the other way round is a page whose labels draw with a number that is not a
  -- face any more.
  if specimen_face ~= nil then
    specimen_face:release()
    specimen_face = nil
  end
end

local function make_label(width, height, text, scale, color, align, face, background)
  local one, err = ui.label(width, height)
  if one == nil then
    console.print('fonts: no label (' .. tostring(err and err.code or 'the memory') .. ')')
    return nil
  end
  one.text = text
  one.scale = scale
  one.color = color or 0
  one.background = background or 15
  one.align = align or 'left'
  one.valign = 'middle'
  if face ~= nil then one:font(face) end
  return one
end

-- --------------------------------------------------------------- la mostra d'una cara

-- **La previsualització d'una cara viu en una sessió de sistema** (0.189.26): the title of a row is the face's own
-- name **drawn with that face** —«DejaVu Sans» in DejaVu Sans—, kept as a picture in a block of the session store and
-- drawn again as a copy, so a page of nine faces does not need nine faces alive (this board keeps three, docs/75).
--
-- **Una sessió per cara, amb una clau que surt del nom**: a session's key is fifteen bytes —`kNameBytes` is sixteen with
-- the terminator— and a file name is longer, so the key is a hash of the name and **the block carries the name it was made
-- from**: two names that landed on the same key are caught by the block and not by luck (docs/30).
--
-- **I el bloc diu de quin fitxer és**: the size of the file goes in the head as well, so the day a face is replaced by
-- another with the same name the sample of the old one is not painted as if it were the new one —the same rule the
-- launcher learned with the versions of its apps— (docs/30, docs/75).
--  **La marca del bloc puja a `FSP3`** (0.189.32): the sample of a name is drawn at the size of `kTitle`, and a title of
--  forty-four pixels that lived in a block is not the title of sixty-six this page draws now —a picture has no way to say
--  «I am small»—, so the head says a marker the block of before does not carry and every sample is drawn once more at the
--  size of today (docs/30, docs/75).
local kSampleMagic = 'FSP3'
local kSampleHeaderBytes = 128          -- the head, in text, and where the pixels start
--  **Un bloc més gran des de la 0.189.32, perquè el títol també ho és** (the owner: *«Fes mes gran tot el text, sobretot el
--  títol»*): the picture of a name is drawn at the size of `kTitle`, so a bigger title is a bigger picture —a name of four
--  hundred and eighty by sixty-six is 31 680 pixels, which is 15 840 B at four bits a pixel— and the block doubles to
--  **16 384 B of pixels** (16 512 B with the head). A sample that still does not fit is drawn all the same and simply not
--  kept (docs/30, docs/75).
local kSamplePixels = 16384             -- the bytes a sample can take: a name of 480 x 66 at four bits per pixel
local kSampleBytes = kSampleHeaderBytes + kSamplePixels

local function sample_key(name)
  -- FNV-1a of thirty-two bits in eight hexadecimal digits: the same name is always the same key, and the head says which
  -- name it was made for.
  local hash = 2166136261
  for i = 1, #name do
    hash = hash ~ name:byte(i)
    hash = (hash * 16777619) & 0xFFFFFFFF
  end
  return string.format('s%08x', hash)
end

-- The head a sample carries, and what it says: the magic, the size of the file it was made from, the box of the picture
-- and the name. A block that says another name or another size is a sample of another face and is drawn again.
local function sample_head(record, w, h)
  return string.format('%s\n%d\n%d %d\n%s\n', kSampleMagic, record.size, w, h, record.name)
end

local function read_sample(block, record)
  local head = block:read(0, kSampleHeaderBytes)
  if head == nil or head == '' then return nil end
  local magic, size, w, h, name = head:match('^(%S+)\n(%d+)\n(%d+) (%d+)\n([^\n]*)\n')
  if magic ~= kSampleMagic then return nil end
  if tonumber(size) ~= record.size or name ~= record.name then return nil end
  w, h = tonumber(w), tonumber(h)
  if w == nil or h == nil or w <= 0 or h <= 0 then return nil end
  if (w * h + 1) // 2 > kSamplePixels then return nil end
  return w, h
end

-- **El títol d'una fila**: the picture of the name, copied from the block when the block has it and drawn with the face
-- when it does not —and kept for the next open if the system gives it a block—. The face is opened and given back inside
-- this call, so a shelf of nine never holds more than one (docs/30, docs/75).
--
-- **I SI LA MEMÒRIA NO ARRIBA PER A UNA MOSTRA, ES TORNA EL BLOC D'UNA FILA QUE NO ES VEU** (0.189.35, l'amo:
-- *«[W][MEMORY] fonts asks for 's12201f79': 16512 B, and the table of 8 is full <- has posat un límit al número de
-- sessions? I el tamany? també està limitat?»*): the eight blocks were a **table of rows** and not a memory, and since the
-- 0.189.36 the kernel has none —a session is a node and it is made while the allocator has room—, so what this door answers
-- now is the memory of the board and not a ceiling of the system. The block is still worth giving back when the ask fails:
-- it is the **copy of the title that survives the app** and not the title itself —the picture is already on the glass in the
-- canvas of its row (`art`)— so freeing sixteen kilobytes of a row that is not on the page for a face that is loses nothing
-- a person can see (docs/30, docs/75).
local function give_a_block_back(first, last)
  local given = false
  for i = 1, #shelf_rows do
    local row = shelf_rows[i]
    if row ~= nil and row.art ~= nil and (i < first or i > last) then
      if sys.session_destroy(sample_key(row.record.name)) ~= nil then
        console.print('fonts: the block of ' .. row.record.name .. ' is given back for this page')
        given = true
      end
    end
  end
  return given
end

local function the_art_of(record, family, width, height, pad, first, last)
  local key = sample_key(record.name)
  local block = sys.session(key, kSampleBytes)
  --  **I el bloc que no hi cabia es demana una segona vegada** (0.189.35): one row of the page that is not on the glass gives
  --  its block back and the ask is made again; a second refusal is the block of a page of faces and the row is drawn anyway.
  if block == nil and first ~= nil and give_a_block_back(first, last) then
    block = sys.session(key, kSampleBytes)
  end
  if block ~= nil then
    local w, h = read_sample(block, record)
    if w ~= nil then
      local art = ui.canvas(w, h)
      if art ~= nil and art:blit(block, kSampleHeaderBytes, 0, 0, w, h) ~= nil then
        art:place(pad, (height - h) // 2)
        console.print(string.format('fonts: sample %s kept %dx%d', record.name, w, h))
        return art
      end
    end
  end

  local face = ui.load_font(record.path)
  if face == nil then return nil end
  local scale = fit_face(face, family, width, kTitle)
  -- **La caixa de la mostra són enters**: a canvas is asked for whole pixels and a measure of a face can come back with a
  -- fraction of one, which is a `canvas` the system refuses instead of a picture (docs/30).
  local w = math.floor(face:text_width(family, scale) or 1)
  local h = math.floor(ui.line_height(scale))
  if w <= 0 then w = 1 end
  if h <= 0 then h = 1 end
  local art = ui.canvas(w, h)
  if art == nil then
    face:release()
    return nil
  end
  --  **Tinta negra sobre paper blanc** (0.189.30): the sample is a title now and the title of a row is written on the paper
  --  of the page; the block of before painted white on black and its marker says so (`FSP1`), o sigui que una mostra vella
  --  no es pintarà mai en una banda blanca (docs/30).
  art:clear(kTitlePaper)
  art:font(face)
  art:text(0, 0, family, scale, 0)
  -- **La cara es dóna abans de desar la imatge**: the picture is pixels and the face is a slot of the board, so it goes
  -- back as soon as it has drawn (docs/30, docs/75).
  face:release()
  art:place(pad, (height - h) // 2)
  local fits = (w * h + 1) // 2 <= kSamplePixels and #sample_head(record, w, h) < kSampleHeaderBytes
  if block ~= nil and fits then
    block:write(0, sample_head(record, w, h))
    if art:capture(block, kSampleHeaderBytes) == nil then
      console.print('fonts: sample ' .. record.name .. ' not kept (no room)')
    else
      console.print(string.format('fonts: sample %s made %dx%d', record.name, w, h))
    end
  elseif block == nil then
    -- **Una placa sense memòria per a això no perd la pàgina**: the title is drawn and the block is what is missing, and
    -- saying it is how a person knows why the next open pays for the face again (docs/30, docs/75).
    console.print('fonts: sample ' .. record.name .. ' not kept (the session store said no)')
  elseif not fits then
    --  **Una mostra que no cap al bloc no es desa**: a name of a hundred letters at the size of the title is more pixels
    --  than the 16 384 B of the block, and the row is drawn all the same (docs/30, docs/75).
    console.print(string.format('fonts: sample %s not kept (%dx%d is more than the block)', record.name, w, h))
  end
  return art
end

-- **A row of the shelf is a control the app fills with a title and three lines in two inks** (0.189.3, and the two papers
-- of the 0.189.30): the sample of the face in the big letters of `kTitle` **on white paper** —the owner: *«Fes mes gran el
-- títol del tipus de lletra i posa fons blanc»*— and the three sentences of the font under it **on a very light grey** and
-- one step bigger than they were (*«a la part d'abaix fes-la gris molt claret de fons. i un tipus de lletra lleugerament mes
-- gran»*). A label paints its own paper, so the boxes do not touch: one over another would cut it in half (docs/30, docs/92).
local function add_entry(the_list, record)
  local entry = ui.list_entry(inner, kRow)
  if entry == nil then return nil end
  local detail = ui.line_height(kDetail)
  local band_h = ui.line_height(kTitle) + 10
  local pad = 8
  --  **Cada fila és una cara i es pot obrir** (0.189.32): the grey row of a file that was not a face —no title band, the
  --  four lines in the ink of what is not there, out of the ring of the dial and with no `on_click`— is gone with the row
  --  itself, because such a file is not listed any more. What is left of that rule is the **fallback of a band that could
  --  not be made**: a canvas is memory, and a row whose canvas the board refused is still a row with its name written in
  --  the face of the system (docs/30, docs/75).
  local name_ink, name_paper, ink = 0, kTitlePaper, 0
  --  **La meitat de sota és un paper propi**: the row is the light grey and the title is the white one over it, which is
  --  what makes the two halves of a row read as two things (docs/30).
  entry.background = kDetailPaper
  local family = family_of(record)
  --  **La banda porta la mostra de la cara i no el nom escrit amb la del sistema** (0.189.26): a canvas of the width of the
  --  row, white, with the picture of the name inside it —copied from the session or drawn with the face—. **It is made
  --  empty here and filled when the row is on the page**: the face of a row nobody can see is a face opened for nothing
  --  (0.189.30, docs/30, docs/75).
  local band = ui.canvas(inner, band_h)
  if band ~= nil then
    band:clear(name_paper)
    band:place(0, 0)
    entry:add(band)
  end
  if band == nil then
    local name = make_label(inner - 2 * pad, band_h, family, fit(family, inner - 2 * pad, kTitle), name_ink, 'left', nil,
      name_paper)
    if name ~= nil then
      name:place(pad, 0)
      entry:add(name)
    end
  end
  local lines = {
    shape_short(record),
    type_of(record) .. '  ' .. bytes_text(record.size),
    record.name,
  }
  local y = band_h + 2
  for i = 1, #lines do
    local scale = fit(lines[i], inner - 2 * pad, kDetail)
    local one = make_label(inner - 2 * pad, detail, lines[i], scale, ink, 'left', nil, kDetailPaper)
    if one ~= nil then
      one:place(pad, y)
      entry:add(one)
    end
    y = y + detail
  end
  entry.on_click = function() open_specimen(record) end
  the_list:add(entry)
  shelf_rows[#shelf_rows + 1] = { record = record, entry = entry, band = band, art = nil, box = { band_h, pad } }
  return entry
end

-- **Només les mostres de la pàgina** (0.189.30, the owner: *«Carrega de les sessions només els tipus de lletra que et
-- calen per aquesta pàgina. Si l'usuari passa pàgina ja carregaràs aquelles. No les carreguis totes de cop»*): a row of the
-- shelf is made empty and its title —a copy from the block, or the face opening and drawing it— is made **when the row is
-- on the glass**. The page is a list of the system and the rows it shows are its own arithmetic, so the count here is the
-- **most** a page can hold (`height // kRow`, without the paginator's footer): a row too many is one sample read for
-- nothing and a row too few is an empty title, and only one of the two is worth erring on (docs/30, docs/75).
--
-- **I el comptador es demana quan el panell ja ha col·locat la llista**: the list is created with the height of one row and
-- a panel that has not been painted yet has not given it the rest of the page, so a count taken at build time is one row
-- and a page would come up with one title. **Un present amb la pàgina aguantada fa el dibuix i no toca el vidre**
-- (0.189.42), o sigui que la pàgina es cou amb la imatge de l'arrencada encara al vidre (docs/30).
local function fill_the_art(page)
  if shelf_list == nil then return end
  local per_page = shelf_list:caps().height // kRow
  if per_page < 1 then per_page = 1 end
  local at = page or shelf_list.page
  if at == nil or at < 1 then at = 1 end
  local first = (at - 1) * per_page + 1
  local last = at * per_page
  if last > #shelf_rows then last = #shelf_rows end
  for i = first, last do
    local row = shelf_rows[i]
    if row ~= nil and row.band ~= nil and row.art == nil then
      local art = the_art_of(row.record, family_of(row.record), inner - 2 * row.box[2], row.box[1], row.box[2], first,
        last)
      if art ~= nil then
        row.band:add(art)
        row.art = art
      end
    end
  end
end

-- **The shelf**: the head of the page and one row per file of the door, in a list that pages itself and says where it is.
build_shelf = function()
  give_back()
  -- **La forma es llegeix abans de col·locar res** (docs/30): la pàgina, el cercle i la fila oberta són el que la persona
  -- va deixar, i una pàgina que es construeix i després es corregeix és una pàgina que es veu saltar.
  local was = load_state()
  if was ~= nil then
    local page = tonumber(was.page)
    if page ~= nil and page >= 1 then shape.page = math.floor(page) end
    shape.focus = was.focus
    shape.marked = was.marked
  else
    shape.page = 1
    shape.focus = nil
    shape.marked = nil
  end
  shelf_rows = {}
  ui.clear(15)
  screen = ui.panel(view.width, view.height, 'vertical')
  if screen == nil then
    console.print('fonts: there is no room for the page')
    return
  end
  screen.padding = margin
  screen.gap = math.floor(margin * 0.6)
  screen:place(0, 0)

  local head = make_label(inner, ui.line_height(kSpecHead), 'Fonts', kSpecHead, 0, 'left')
  if head ~= nil then screen:add(head) end
  -- **I el subtítol diu quantes cares hi ha i quants fitxers s'han deixat fora** (0.189.32): the count of the rows and the
  -- count of what the door had and the page does not show, which are the two numbers a person checks when a face they know
  -- is not there (docs/30).
  local sentence = string.format('%d faces on /fonts', #files)
  if left_out > 0 then
    sentence = sentence .. string.format(', %d file(s) left out', left_out)
  end
  local summary = make_label(inner, ui.line_height(kSpecNote), sentence, fit(sentence, inner, kSpecNote), 8, 'left')
  if summary ~= nil then screen:add(summary) end

  -- **The list takes the rest of the page and the panel says how much it is** (0.174.3, the owner: *«i perqué el control
  -- list no ocupa tota la vista fins abaix?»*): `weight` is the field a panel reads to split what is left after the cells
  -- that keep their size, so the box of the list ends at the bottom margin of the page and the rows fill it —three lines of
  -- the head measured by hand were a guess that left two rows of paper at the bottom of the glass—. **A list that fills
  -- the page fits more rows and pages less**, which is the point of filling it (docs/30, docs/90).
  local the_list = ui.list(inner, kRow)
  if the_list == nil then
    console.print('fonts: there is no room for the list')
    return
  end
  the_list.row_height = kRow
  the_list.weight = 1
  screen:add(the_list)
  local first = nil
  for i = 1, #files do
    local entry = add_entry(the_list, files[i])
    if i == 1 then first = entry end
  end
  -- **La pàgina, la marca i el cercle que la persona tenia** (docs/30): the page is set once the rows are in —a list counts
  -- its pages from its rows and its box— and the values are **clamped**: a shelf that had five pages yesterday and has
  -- three today cannot come back on a page that does not exist, and a name that is not in the door any more is not a ring
  -- but a page with no ring at all —the ring goes to the row of the state, or to the first one—.
  if shape.page > 1 then the_list.page = shape.page end
  local marked_at = 0
  local wanted = nil
  for i = 1, #shelf_rows do
    if shape.marked ~= nil and shelf_rows[i].record.name == shape.marked then marked_at = i end
    if shape.focus ~= nil and shelf_rows[i].record.name == shape.focus then wanted = shelf_rows[i].entry end
  end
  if marked_at > 0 then the_list.marked = marked_at end
  if wanted ~= nil then
    ui.focus(wanted)
  elseif first ~= nil then
    ui.focus(first)
  end
  -- **I el que ha quedat és el que es desa**: el sistema pot haver acotat la pàgina a les que hi ha avui.
  shape.page = the_list.page
  save_state(shape)
  -- **I les mostres que aquesta pàgina necessita** (0.189.30): the rows exist and the page is the one the state asked for.
  shelf_list = the_list
  -- **La pàgina es diu, i la forma es desa a l'instant** (docs/30): the paginator is the list's and a swipe or a key turns
  -- it, so the app is told which page it is on and nothing has to read the gesture to know (the rule of 0.179.4) — and
  -- turning it is a change of the view, which is a kilobyte copied to the PSRAM and not a file (0.179.51, docs/85).
  -- **I una pàgina que gira porta les seves mostres** (0.189.30): the face of a row that was not on the glass was not opened
  -- and is opened now, which is the whole of what a page turn costs.
  the_list.on_page = function(self, page)
    shape.page = page
    save_state(shape)
    console.print('fonts: page ' .. page)
    fill_the_art(page)
    ui.present()
  end
  -- **LA PÀGINA ES COU AMB LA IMATGE ENCARA AL VIDRE I PUJA SENCERA** (0.189.42, l'amo: *«veig que l'splash s'envà just abans
  -- de que les fonts es posin a la vista.. podriem treure l'splash després?»*): the list is given its box by the draw a present
  -- makes and the titles are made from that box, so the page is composed **without reaching the glass** —`ui.hold_page(true)`—,
  -- the titles go into it, and the release paints the page **whole and clean in one pass**. Before this the debt was paid
  -- right here: the picture went away, the rows arrived without their names and the names came after as patches, which is
  -- exactly the order a person saw (docs/30).
  ui.hold_page(true)
  ui.present()
  fill_the_art()
  ui.present()
  ui.hold_page(false)
  console.print(string.format('fonts: %d rows, %d file(s) of the door are not faces', #files, left_out))
  -- **And where each row is, for the test that has no finger of its own** (docs/85): the place of a row is the list's
  -- plus the row's, which are the two boxes the system gave them.
  if debug_boxes and shelf_list ~= nil then
    -- **The place of a row is the sum of its frames** (docs/30): the list's own place inside the view and the row's
    -- inside the list, which is what a finger on the glass is measured against.
    local at = shelf_list:caps()
    for i = 1, #shelf_rows do
      local row = shelf_rows[i].entry:caps()
      console.print(string.format('fonts: row %s %d %d', shelf_rows[i].record.name,
        at.x + row.x + row.width // 2, at.y + row.y + row.height // 2))
    end
    --  **I la caixa de la barra de l'arrencada, en files del vidre** (0.189.40): the bar is gone by now —the shelf is on the
    --  glass— and its place is what the case of a start measures (docs/85).
    if start_boxes ~= nil then
      console.print(string.format('fonts: startbar %d %d %d %d', start_boxes.x, start_boxes.y, start_boxes.w,
        start_boxes.h))
    end
  end
end

-- **The specimen**: the face's name, what the system made of it, and the same words in its own letters at four sizes.
-- The face is loaded when the page is built and given back when it goes, because three of them is all this board keeps
-- (docs/30, docs/75).
open_specimen = function(record)
  if record == nil or record.kind == nil then return end
  -- **La fila oberta és la forma de la vista i es desa abans de pintar la pàgina** (docs/30): una app que es tanca amb
  -- l'exemplar al vidre ha de tornar amb l'exemplar al vidre.
  shape.marked = record.name
  save_state(shape)
  give_back()
  ui.clear(15)

  local face, err = ui.load_font(record.path)
  if face == nil then
    console.print('fonts: cannot open ' .. record.path .. ' ' .. tostring(err and err.code))
    build_shelf()
    return
  end
  local width, height, glyphs = face:size()
  record.width = width
  record.height = height
  record.glyphs = glyphs
  -- **And the page keeps it**: see `give_back()` for the two ways a face that is nobody's goes wrong.
  specimen_face = face

  screen = ui.panel(view.width, view.height, 'vertical')
  if screen == nil then
    face:release()
    specimen_face = nil
    console.print('fonts: there is no room for the page')
    return
  end
  screen.padding = margin
  screen.gap = math.floor(margin * 0.5)
  screen:place(0, 0)

  local function line(text, scale, color, use_face)
    local one = make_label(inner, ui.line_height(scale), text, scale, color, 'left', use_face and face or nil)
    if one ~= nil then screen:add(one) end
    return one
  end

  -- **The name of the page is the app's face and not the one on show**: a face of six letters cannot write its own file
  -- name, and a specimen whose head is a white box is a page that looks broken (found on the emulator with the sample
  -- bdf, 10/11). What the face draws is the specimen under it, and nothing else (docs/30, docs/92).
  line(record.name, fit(record.name, inner, kSpecHead), 0)
  line(record.kind .. '  ' .. bytes_text(record.size), kSpecNote, 8)
  line(shape_of(record), kSpecNote, 8)
  if record.glyphs > 0 and record.glyphs < 26 then
    -- **A face of six glyphs is not a broken face**: it draws the letters it carries and nothing else, and saying so is
    -- the difference between a face and a page that lost its words (docs/30).
    line('a small face: it draws only the codes it carries', kSpecNote, 8)
  end

  -- **A face of bits is read at its own size and the sizes that multiply it**: its glyph is a cell of pixels, and a cell
  -- blown up four times is a blob and not a bigger letter —the difference with a face of outlines, said on the glass—
  -- (docs/30). The three steps of a bitmap face are its table and two doubles of it.
  local steps = kSample
  if record.width > 0 then
    local biggest = math.min(3, kSample[1])
    steps = { biggest, biggest, math.min(2, biggest), 1 }
    local bit_note = string.format('a bitmap face: %dx%d a cell; bigger is a double', record.width, record.height)
    line(bit_note, fit(bit_note, inner, kSpecNote), 8)
  end

  -- The specimen itself: one word big and the rows a face is read by, each at the size it is read at **and no bigger
  -- than the glass**: a row that ends in "..." is a row a person cannot read (docs/30).
  line('Hamburgefonstiv', fit_face(face, 'Hamburgefonstiv', inner, steps[1]), 0, true)
  line('ABCDEFGHIJKLMNOPQRSTUVWXYZ', fit_face(face, 'ABCDEFGHIJKLMNOPQRSTUVWXYZ', inner, steps[2]), 0, true)
  line('abcdefghijklmnopqrstuvwxyz', fit_face(face, 'abcdefghijklmnopqrstuvwxyz', inner, steps[3]), 0, true)
  line('0123456789 .,;:!?()[]', fit_face(face, '0123456789 .,;:!?()[]', inner, steps[3]), 0, true)
  line('The quick brown fox jumps over the lazy dog.',
    fit_face(face, 'The quick brown fox jumps over the lazy dog.', inner, steps[4]), 0, true)
  -- **What a face does not carry is the other half of a specimen**: the accented letters of this page are in the row
  -- below, and a face whose table stops at the printable ascii draws them as nothing —which a person reads as a broken
  -- app unless the page says it— (docs/30).
  -- **And the row is written as bytes and not as text** (0.178.4): the text of this system is one byte a character
  -- and Latin-1 when the face is (docs/30), so a row written in the UTF-8 of this file drew «Ã€ Ã‰…» —two letters a
  -- person cannot read— where the specimen should have drawn «À É…». `string.char` is how the calculator writes its
  -- signs, and it is how a page of this system says a letter that is not ascii.
  -- **I LA FILA DIU QUÈ ÉS** (0.189.35, l'amo: *«On posa "l'hora i el rellotge" de l'app Fonts, a la vista detall de la
  -- font, que significa? es una especie de placeholder?»*): no era cap rellotge ni cap farciment — era una frase en
  -- català triada **perquè porta accents dins de paraules** (`l'hora`, `rellotge`) i la vista és en anglès, o sigui que
  -- una persona no podia saber de què anava. The row is the accented letters of the Latin-1 table and then a sentence
  -- that says so, read at the size of the specimen like the four rows above it.
  local accents = string.char(0xC0, 0xC9, 0xCD, 0xD3, 0xDA, 0x20, 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0x20, 0xC7, 0xE7,
                              0x20, 0xB7, 0x20) .. 'the accented letters of a page'
  line(accents, fit_face(face, accents, inner, steps[3]), 0, true)
  -- **And the sentence under it is the whole truth of a face**: it draws the codes it carries and nothing for the rest,
  -- which is what a person sees when the letters with a tilde come out as gaps (docs/30).
  local note = 'the codes a face has not are blank'
  line(note, fit(note, inner, kSpecNote), 8)

  -- **And the way back, at the very bottom**: the pinch of the glass takes the view away too (`on_back`), and this button
  -- is for a person with a finger who does not know the gesture (docs/30). **I la marca al costat de la paraula**
  -- (0.189.31, the owner: *«amb una icona de "back" a l'esquerra i el text a la dreta»*), which is the mark of the
  -- system and not a drawing of this file (docs/30, docs/92).
  --
  -- **I PETIT I DISCRET, ENCARA DES DE L'ESQUERRA** (0.189.33, l'amo: *«Fes el botó mes petit i discret, que no ocupi tot
  -- l'ample, alineat a l'esquerra»*): a button of the width of the column was a door as wide as the page and a mark of a
  -- hundred and eight pixels —a picture of sixty-four blown up, which on this glass is a smear with an edge of steps, and
  -- that is what «l'icona es horrible» was—. Now it is `kDoorW` by `kDoorH`, the mark is a square of sixty-eight and comes
  -- out of the picture at its own size, and the word is `Back`: a door and not a bar (docs/30, docs/92).
  --
  -- **I va dins d'una fila perquè el panell estira els fills a l'ample**: a column gives every child the width of the
  -- column, so a narrower button sat in it would be pulled to the whole page again; inside a row of horizontal flow a
  -- child keeps the width it was made with and the row, which is the column's, puts it on the left (docs/30, docs/90).
  --
  -- **Es fa abans que el requadre i s'afegeix després** (0.189.31): the first focusable control to appear takes the focus of
  -- the page, and a box of lines **without a frame** draws no ring —it is a page of a book, not a field— so a page whose box
  -- took the focus would come up with a focus nobody can see. The button takes it, and the order of the column is the order
  -- of `add` and not the order of this file (docs/30, docs/90).
  local back = ui.button(kDoorW, kDoorH)
  if back ~= nil then
    back.title = 'Back'
    back.scale = kBody
    back.icon = 'back'
    back.icon_side = 'left'
    back.on_click = function() go_back() end
  end

  -- **Els passatges del llibre, entre els detalls i la manera de tornar** (0.189.31, the owner: *«Entre els detalls i el
  -- botó un TextBoxMultiline sense vores i read only ple de text»*): a box of lines with **no frame of its own** —it is a
  -- page of a book on the paper of the page and not a field to write in— which is **read and not written** (`readonly`:
  -- the caret is kept and the keyboard never comes up), **wraps** the paragraphs of `kPassages` at the measure of the
  -- glass and takes **the room the details and the button leave** with a `weight`, so the page of a face of nine lines
  -- and the page of a face of four draw the same way (docs/30, docs/90).
  --
  -- **I EL TEXT ÉS DE LA CARA QUE S'ESTÀ MIRANT** (0.189.33, l'amo: *«no sembla que sigui el tipus de lletra de dalt, fes el
  -- tipus de lletra mes gran»*): the box was drawing its passages with the face of the system while the page above it drew
  -- the face of the file, and a page of a face where the prose under it is another face is a page that lies. `cv:font` gives
  -- the control the face of the specimen —the one the page keeps alive for its labels— and the rows wrap at that face's own
  -- measure; the size goes up to `kSpecNote`, which is a body of four steps (docs/30, docs/90).
  local passages = ui.textbox_multiline(inner, ui.line_height(kSpecNote) * 4)
  if passages ~= nil then
    passages.text = kPassages
    passages.scale = kSpecNote
    passages.color = 0
    passages.background = 15
    passages.framed = false
    passages.readonly = true
    passages.caret = false
    passages.wrap = true
    passages.top = 0
    passages.weight = 1
    -- **Una cara de bits es llegeix al seu doble i no més** (0.189.33): the page says it two lines above with a sentence of
    -- its own, and a box of prose blown up four times would contradict it on the same glass —the rule `kSample` follows for
    -- the specimen (docs/30).
    if record.width > 0 then passages.scale = math.min(2, kSpecNote) end
    passages:font(face)
    -- **I el dit el mou**: the bar of the box is the system's and the gesture is the app's, and a box that registers none
    -- is a box whose passages could only be read by the rows that fit on the glass (docs/90).
    passages.on_swipe = function(self, rows) end
    screen:add(passages)
  end
  -- **La fila del botó, al final de la columna i amb el botó a l'esquerra** (0.189.33).
  local door_row = nil
  if back ~= nil then
    door_row = ui.panel(inner, kDoorH, 'horizontal')
    if door_row ~= nil then
      door_row.padding = 0
      door_row.gap = 0
      door_row:add(back)
      screen:add(door_row)
    else
      screen:add(back)
    end
  end

  ui.present()
  console.print(string.format('fonts: open %s %s %d %d', record.name, record.kind, record.size, record.glyphs))
  if debug_boxes and back ~= nil then
    -- **La caixa del botó és dins de la fila i la fila dins del panell** (0.189.33): `caps` gives the box a control has in
    -- its father, so the place of the door is the row's plus its own —which is the sum this page already does for the rows
    -- of the shelf— (docs/30, docs/85).
    local at = { x = 0, y = 0 }
    if door_row ~= nil then
      local row = door_row:caps()
      at.x, at.y = row.x, row.y
    end
    local box = back:caps()
    local left, top = at.x + box.x, at.y + box.y
    console.print(string.format('fonts: backbox %d %d', left + box.width // 2, top + box.height // 2))
    -- **On és el botó i on el requadre dels passatges** (0.189.31): the case that presses the way back needs the box of the
    -- button, and the case that says the page fits needs the box the passages were given and the two rows the button takes,
    -- which are the two things a layout change moves (docs/30, docs/85).
    console.print(string.format('fonts: backrows %d %d', top, top + box.height))
    if passages ~= nil then
      local room = passages:caps()
      console.print(string.format('fonts: passagebox %d %d %d %d', room.x, room.y, room.width, room.height))
      console.print(string.format('fonts: passagerows %d %d', passages:rows(), passages:lines()))
    end
  end
  app.view_push()
end

go_back = function()
  app.view_pop()
  console.print('fonts: back')
  -- **El cercle es queda a la fila que s'acaba de tancar** (docs/30): tornar del exemplar i trobar el cercle a la primera
  -- fila és tornar a buscar el que la persona tenia a la mà.
  shape.focus = shape.marked
  shape.marked = nil
  save_state(shape)
  build_shelf()
end

-- **A pinch took the specimen away** (0.163.0): the system has already given the view back, so this only puts the shelf
-- on the glass again and does not pop a second time (docs/30).
function on_back(depth)
  console.print(string.format('fonts: back by the gesture, %d views left', depth))
  -- **El pessic deixa la pàgina com la deixa el botó** (docs/30): el cercle a la fila que s'ha tancat i cap exemplar obert.
  shape.focus = shape.marked or shape.focus
  shape.marked = nil
  save_state(shape)
  build_shelf()
end

-- --------------------------------------------------------------------- the app

-- **La barra de l'arrencada es fa aquí i es deixa anar abans del prestatge**: a page that is built again —going back, a page
-- that turns— has nothing of the start in it, and a control of the start that survived the first present would be a bar stuck
-- in the middle of the shelf for ever (docs/30).
build_layout()
local the_start = raise_the_bar()
read_the_shelf(the_start)
drop_the_bar(the_start)
build_shelf()

-- **A font named by the prompt opens on its page** (0.65.0): `run fonts /fonts/x.ttf` is a person saying what they want
-- to look at, and a viewer that starts on the shelf would make them look for it. It is the same road the editor and the
-- reader take with a file (docs/30, docs/40).
local named = app.args and app.args.path
if named ~= nil and named ~= '' then
  local record = { name = named:match('([^/]+)$') or named, path = named }
  local kind, why = sniff(named)
  local stat = fs.stat(named)
  record.kind = kind
  record.why = why
  record.size = (stat and stat.size) or 0
  console.print('fonts: asked for ' .. named)
  if kind ~= nil then open_specimen(record) end
elseif shape.marked ~= nil then
  -- **I si no hi ha cap camí dit, la pàgina torna amb l'exemplar que la persona tenia obert** (0.189.27, docs/30): el
  -- prestatge ja ha posat el cercle i la marca de la fila que el bloc demanava, i obrir-la és el mateix camí que fa un dit.
  -- Un nom que ja no és a la porta no obre res: el prestatge es queda com està, que és el seu estat sense cap exemplar.
  for i = 1, #files do
    if files[i].name == shape.marked and files[i].kind ~= nil then
      console.print('fonts: came back to ' .. files[i].name)
      open_specimen(files[i])
      break
    end
  end
end

-- **Les tres portes del comiat** (docs/30, docs/40): the shape is saved here as well, because an app does not always see
-- the end coming —a raise, the ESC, a `poweroff`— and a page that is only written when it changes is a page that loses
-- the last change of a person who left in a hurry.
function on_pause() save_state(shape) end
function on_save() save_state(shape) end
function on_close() save_state(shape) end

-- The page is a tree of controls and the finger, the dial and the keys are the system's: what this loop has to do is
-- keep the kernel turning and offer the page again when something of it has changed. A present with nothing new draws
-- nothing (docs/30, docs/90).
while true do
  --  **Les mostres de la pàgina es fan a `build_shelf`** (0.189.42): the titles are in the page before it reaches the glass,
  -- so a turn that finds nothing new draws nothing at all (docs/30, docs/75).
  ui.present()

  -- The system's gestures are taken and dropped, so that a queue nobody empties does not grow (the same rule the
  -- calculator follows, docs/30). The pinch of a view is the system's and arrives as `on_back`.
  --
  -- **I el swipe a l'esquerra és el camí de tornada** (0.189.35, l'amo: *«A la vista detall registra el swipe a l'esquerra
  -- per fer "back"»*): the gesture that goes back is the one a person already does on a list —the finger to the left,
  -- `kBackSlop` pixels or more, and more to the sides than up or down— and it takes the same road as the door (`go_back`:
  -- the view goes and the circle stays on the row that was open). `specimen_face` is what says a specimen is on the glass,
  -- because the drag of the shelf has nothing to go back to and is still dropped (docs/30).
  --
  -- **I CAP A LA DRETA TAMBÉ** (0.189.37, l'amo: *«com el faig cap a la dreta?»*): a person does not know which way this
  -- page expects —a list on a phone is put away with the finger to the right and a book turns with it to the left—, so the
  -- page answers for the two: `kBackSlop` pixels or more **to either side**, and more sideways than up or down. A drag that
  -- is mostly up or down is still the box of the passages scrolling, and the sign of `dx` is what the log says of the
  -- finger that went (`fonts: back by the swipe, 240 px` is a drag to the right) (docs/30).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'drag' and specimen_face ~= nil and math.abs(gesture.dx) >= kBackSlop and
      math.abs(gesture.dx) > math.abs(gesture.dy) then
      console.print(string.format('fonts: back by the swipe, %d px', gesture.dx))
      go_back()
    end
    gesture = touch.gesture()
  end

  sys.delay(50):await()
end
