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

local caps = sys.caps()
if not caps.display then
  console.print('this board needs a glass for this')
  return
end

-- The whole page when it changes and a `du` when only the answer of a tap is on it: on e-ink that is the difference
-- between a page of text and a page that flashes (docs/50).
local kPageMode = 'gc16'

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
-- **The same text in four sizes**, which is what a specimen is: a face of outlines is drawn at each of them and a bitmap
-- face is multiplied, and a person sees that with one look (docs/30).
local kSample = { 7, 4, 3, 2 }

local view = ui.caps()
local margin = 0
local kRow = 0
local inner = 0

local function build_layout()
  view = ui.caps()
  margin = math.max(10, view.width // 24)
  inner = view.width - 2 * margin
  -- **A row is never under fifty-two pixels**: it is what a fingertip needs, and a row of this page carries a name and a
  -- sentence beside it (docs/90).
  kRow = math.max(72, math.floor(view.height * 0.082))
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

-- The files of the door, by name, with what each one is. The bytes are the file's and not a guess: `fs.list` gives them
-- and the shelf shows them, because a face of two kilobytes and one of twenty are two different things to a person
-- (docs/30).
local files = {}
local faces = 0

local function read_the_shelf()
  local entries = fs.list('/fonts')
  files = {}
  faces = 0
  if entries == nil then
    console.print('fonts: /fonts cannot be listed')
    return
  end
  for i = 1, #entries do
    local one = entries[i]
    if one.type == 'file' then
      local kind, why, width, height, glyphs = probe('/fonts/' .. one.name)
      local record = {
        name = one.name,
        path = '/fonts/' .. one.name,
        size = one.size or 0,
        kind = kind,
        why = why,
        width = width or 0,
        height = height or 0,
        glyphs = glyphs or 0,
      }
      files[#files + 1] = record
      if kind ~= nil then
        faces = faces + 1
        console.print(string.format('fonts: %s %s %d', record.name, kind, record.size))
      else
        console.print(string.format('fonts: %s none %d %s', record.name, record.size, tostring(why)))
      end
    end
  end
  table.sort(files, function(a, b) return a.name:lower() < b.name:lower() end)
end

-- A number of bytes the way a person reads it, which is what the detail of a row says.
local function bytes_text(size)
  if size >= 1024 then
    return string.format('%.1f kB', size / 1024)
  end
  return string.format('%d B', size)
end

-- The sentence of a record: the format and the bytes, or the reason it is not a face.
local function detail_of(record)
  if record.kind == nil then return 'not a face' end
  return record.kind .. '  ' .. bytes_text(record.size)
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

local function make_label(width, height, text, scale, color, align, face)
  local one, err = ui.label(width, height)
  if one == nil then
    console.print('fonts: no label (' .. tostring(err and err.code or 'the memory') .. ')')
    return nil
  end
  one.text = text
  one.scale = scale
  one.color = color or 0
  one.background = 15
  one.align = align or 'left'
  one.valign = 'middle'
  if face ~= nil then one:font(face) end
  return one
end

-- **A row of the shelf is a control the app fills**: the name on its own line and the sentence of the format under it,
-- in two boxes that do not touch —a label paints its own paper, so a second one over the first would cut it in half—
-- (docs/30, docs/92).
local function add_entry(the_list, record)
  local entry = ui.list_entry(inner, kRow)
  if entry == nil then return nil end
  local name_h = kRow - ui.line_height(kSmall) - 6
  local name = make_label(inner - 24, name_h, record.name, kBody, record.kind and 0 or 8, 'left')
  if name ~= nil then
    name:place(12, 1)
    entry:add(name)
  end
  local detail = make_label(inner - 24, ui.line_height(kSmall), detail_of(record), kSmall, 8, 'left')
  if detail ~= nil then
    detail:place(12, name_h + 2)
    entry:add(detail)
  end
  if record.kind ~= nil then
    entry.on_click = function() open_specimen(record) end
  end
  the_list:add(entry)
  shelf_rows[#shelf_rows + 1] = { record = record, entry = entry }
  return entry
end

-- **The shelf**: the head of the page and one row per file of the door, in a list that pages itself and says where it is.
build_shelf = function()
  give_back()
  shelf_rows = {}
  ui.clear(15, kPageMode)
  screen = ui.panel(view.width, view.height, 'vertical')
  if screen == nil then
    console.print('fonts: there is no room for the page')
    return
  end
  screen.padding = margin
  screen.gap = math.floor(margin * 0.6)
  screen:place(0, 0)

  local head = make_label(inner, ui.line_height(kBig), 'Fonts', kBig, 0, 'left')
  if head ~= nil then screen:add(head) end
  local sentence = string.format('%d files and %d faces on /fonts', #files, faces)
  local summary = make_label(inner, ui.line_height(kSmall), sentence, fit(sentence, inner, kSmall), 8, 'left')
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
  -- **The ring is on the first row**: a list with entries is a list of stops, and a page that does not say which one the
  -- centre would press is a page opened only by a finger (docs/30).
  if first ~= nil then ui.focus(first) end
  shelf_list = the_list
  ui.present(kPageMode)
  console.print(string.format('fonts: %d rows, %d of them faces', #files, faces))
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
  end
end

-- **The specimen**: the face's name, what the system made of it, and the same words in its own letters at four sizes.
-- The face is loaded when the page is built and given back when it goes, because three of them is all this board keeps
-- (docs/30, docs/75).
open_specimen = function(record)
  if record == nil or record.kind == nil then return end
  give_back()
  ui.clear(15, kPageMode)

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
  line(record.name, fit(record.name, inner, kBig), 0)
  line(record.kind .. '  ' .. bytes_text(record.size), kSmall, 8)
  line(shape_of(record), kSmall, 8)
  if record.glyphs > 0 and record.glyphs < 26 then
    -- **A face of six glyphs is not a broken face**: it draws the letters it carries and nothing else, and saying so is
    -- the difference between a face and a page that lost its words (docs/30).
    line('a small face: it draws only the codes it carries', kSmall, 8)
  end

  -- **A face of bits is read at its own size and the sizes that multiply it**: its glyph is a cell of pixels, and a cell
  -- blown up four times is a blob and not a bigger letter —the difference with a face of outlines, said on the glass—
  -- (docs/30). The three steps of a bitmap face are its table and two doubles of it.
  local steps = kSample
  if record.width > 0 then
    local biggest = math.min(3, kSample[1])
    steps = { biggest, biggest, math.min(2, biggest), 1 }
    local bit_note = string.format('a bitmap face: %dx%d a cell; bigger is a double', record.width, record.height)
    line(bit_note, fit(bit_note, inner, kSmall), 8)
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
  line('ÀÉÍÓÚ àèìòù Çç · l\'hora i el rellotge', kSample[3], 0, true)
  -- **And the sentence under it is the whole truth of a face**: it draws the codes it carries and nothing for the rest,
  -- which is what a person sees when the letters with a tilde come out as gaps (docs/30).
  local note = 'the codes a face has not are blank'
  line(note, fit(note, inner, kSmall), 8)

  -- **And the way back**: the pinch of the glass takes the view away too (`on_back`), and this button is for a person
  -- with a finger who does not know the gesture (docs/30).
  local back = ui.button(inner, kRow)
  if back ~= nil then
    back.title = 'Back to the shelf'
    back.scale = kBody
    back.on_click = function() go_back() end
    screen:add(back)
  end

  ui.present(kPageMode)
  console.print(string.format('fonts: open %s %s %d %d', record.name, record.kind, record.size, record.glyphs))
  if debug_boxes and back ~= nil then
    local box = back:caps()
    console.print(string.format('fonts: backbox %d %d', box.x + box.width // 2, box.y + box.height // 2))
  end
  app.view_push()
end

go_back = function()
  app.view_pop()
  console.print('fonts: back')
  build_shelf()
end

-- **A pinch took the specimen away** (0.163.0): the system has already given the view back, so this only puts the shelf
-- on the glass again and does not pop a second time (docs/30).
function on_back(depth)
  console.print(string.format('fonts: back by the gesture, %d views left', depth))
  build_shelf()
end

-- --------------------------------------------------------------------- the app

build_layout()
read_the_shelf()
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
end

-- The page is a tree of controls and the finger, the dial and the keys are the system's: what this loop has to do is
-- keep the kernel turning and offer the page again when something of it has changed. A present with nothing new draws
-- nothing (docs/30, docs/90).
while true do
  ui.present()

  -- The system's gestures are taken and dropped, so that a queue nobody empties does not grow (the same rule the
  -- calculator follows, docs/30). The pinch of a view is the system's and arrives as `on_back`.
  local gesture = touch.gesture()
  while gesture do
    gesture = touch.gesture()
  end

  sys.delay(50):await()
end
