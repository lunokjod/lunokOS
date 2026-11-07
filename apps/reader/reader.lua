-- reader: the app of a book — the shelf, the page, the foot and where you were (docs/46, C1..C13).
--
-- **The app does not lay out anything.** The engine behind a page is the system's —the zip, the tokenizer, the
-- paginator and the face— and this file is what a person touches: which book, which page, and the number that says
-- how far in they are (docs/46, §1). Everything else here is the shelf, the two buttons and the file of positions.
--
-- **The position of a book is a chapter and a byte**, not a page number, because a page is as many words as fit a
-- box of a machine: `epub.page` says the byte the next page starts at and the app keeps it. Going forward is that
-- byte; going back is walking the chapter from zero with `measure`, which lays the pages out and paints nothing
-- (docs/46, B4) — a chapter of a book is a few dozen pages and walking them is cheaper than keeping a table that
-- the screen would have to be told about anyway.
--
-- What it does not do, said here so nobody looks for it: no dark page, no cover and no thumbnails (C11 needs a cache of
-- images and a `save` of a surface), and the chapter is the first one that looks like a chapter until the OPF is read in
-- Lua (C2).

local caps = sys.caps()
if not caps.display then
  console.print('reader: this board has no glass')
  return
end

-- **The whole glass** (0.136.0, docs/46 D1): a page of a book is a page and the strip of the system takes forty-eight
-- rows of it. The app asks for the glass here and the system answers with `on_resize`, which is a hook the app already
-- knows; three taps on the glass give the bar back and the same hook says so again.
ui.full_screen(true)
ui.title('Library')

-- **The size of the view is read every time the system gives another one** (0.136.0): asking for the whole glass and
-- giving it back are two of those moments, and a canvas of the size of before is a page with a strip of paper left over.
local kFoot = 40
local view = { w = 0, h = 0 }
local kPageW = 0
local kPageH = 0

local function use_view(width, height)
  view.w = width
  view.h = height
  kPageW = width
  kPageH = height - kFoot
end

do
  local now = ui.caps()
  use_view(now.width, now.height)
end

-- **The face of the book** (docs/46, C13): the serif of the disk with the letters of Latin-1, and the system's if
-- the disk does not carry it. The step is four, which is the thirty-six pixels of body of the paper project.
local kBody = 4
local face = ui.load_font('/fonts/dejavuserif-latin1.ttf')
if face == nil then
  console.print('reader: no serif face on the disk, the system face reads the book')
end

-- **The position of each book**, kept by the app: one line per book with the path, the chapter and the byte
-- (docs/46, C6). A file that cannot be written is a lost position and not a lost book.
-- **The positions live in the app's own data** and not beside the book: an app of this system writes in its own
-- folder and reads the rest of the machine, which is the rule that keeps one app out of another's papers
-- (docs/30, docs/70). The day the system gives an app a writable shelf, this line is the one that changes.
local kReading = (function()
  local ok, folder = pcall(fs.data)
  if ok and type(folder) == 'string' and folder ~= '' then return folder .. '/reading.txt' end
  return '/data/reading.txt'
end)()

local function positions()
  local out = {}
  local ok, data = pcall(fs.read, kReading)
  if not ok or type(data) ~= 'string' then return out end

  for line in data:gmatch('[^\n]+') do
    local path, chapter, byte = line:match('^(.-)\t(.-)\t(%d+)$')
    if path ~= nil then out[path] = {chapter = chapter, byte = tonumber(byte) or 0} end
  end

  return out
end

local function remember(path, chapter, byte)
  local kept = positions()
  kept[path] = {chapter = chapter, byte = byte}

  local lines = {}
  for one, where in pairs(kept) do
    lines[#lines + 1] = string.format('%s\t%s\t%d', one, where.chapter, where.byte)
  end
  table.sort(lines)

  pcall(fs.write, kReading, table.concat(lines, '\n') .. '\n')
end

-- **The shelf is the reader's** and not the file system of the app (docs/46, C1): the folder of the books is the
-- system's, and an app that could walk the flash would be an app with a key to every room. `epub.books` answers the
-- paths and the app makes the title out of the name of the file.
local function shelf_of()
  local out = {}
  local ok, paths = pcall(epub.books)
  if not ok or type(paths) ~= 'table' then return out end

  for _, path in ipairs(paths) do
    local name = path:match('([^/]+)%.epub$')
    if name ~= nil then out[#out + 1] = { path = path, title = name } end
  end

  return out
end

-- What is on the page, and the two screens are two different sets of controls: a screen is built and given back,
-- never hidden (docs/30, docs/90).
local screen = nil
local books = {}
local book = nil
local chapter = nil
local title = ''
local bytes = 0
local from = 0
local next_at = 0
local ended = false
local foot = nil

local function percent()
  if bytes <= 0 then return 0 end
  return math.floor(from * 100 / bytes + 0.5)
end

local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
  foot = nil
end

local function present_page()
  if screen == nil or book == nil then return end

  screen:clear(15, 'gc16')
  local nxt, done, used, picture = epub.page(screen, from, {
    face = face,
    scale = kBody,
    title_scale = kBody + 1,
    small_scale = kBody - 1,
  })
  if nxt == nil then
    foot.text = 'this page cannot be composed'
    ui.present('gc16')
    return false
  end

  next_at = nxt
  ended = done
  foot.text = string.format('%s   %d%%', title, percent())
  ui.present('gc16')

  return true
end

local function forget_page()
  if chapter ~= nil and book ~= nil then remember(book.path, chapter, from) end
end

-- **The page of the book, built again when the view changes size** (0.136.0, docs/46 D1): a canvas of the new size, the
-- foot under it, and the page drawn in it. The system makes a new view and the objects of before point at a canvas
-- nobody has any more, so this is a build and not a move (docs/30, docs/90).
local function build_page()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
  foot = nil
  if book == nil then return false end

  screen = ui.canvas(kPageW, kPageH)
  if screen == nil then
    console.print('reader: there is no memory for a page')
    return false
  end
  screen:place(0, 0)
  if face ~= nil then screen:font(face) end

  foot = ui.label(kPageW, kFoot)
  if foot ~= nil then
    foot.text = ''
    foot.scale = 2
    foot.align = 'left'
    foot.color = 0
    foot.background = 15
    foot:place(8, kPageH)
  end

  return present_page()
end

-- **The page after this one** is the byte the page said, and there is nothing to compute (docs/46, §1).
local function turn_forward()
  if ended or screen == nil or book == nil then return end

  from = next_at
  forget_page()
  present_page()
end

-- **The page before this one** is the last page that started before the byte we are at, which is found by walking
-- the chapter with `measure`: the same arithmetic and not a pixel (docs/46, B4).
local function turn_back()
  if screen == nil or book == nil or from == 0 then return end

  local walk = 0
  local last = 0
  while true do
    local nxt = epub.page(screen, walk, { face = face, scale = kBody, measure = true })
    if nxt == nil or nxt <= walk or nxt > from then break end
    last = walk
    walk = nxt
  end

  from = last
  forget_page()
  present_page()
end

local function open_book(one)
  give_back()

  local ok, why = epub.open(one.path)
  if not ok then
    console.print('reader: ' .. tostring(why.message))
    return false
  end

  book = one
  ui.title(one.title)

  local where = positions()[one.path]
  chapter = (where ~= nil) and where.chapter or epub.first_chapter()
  local n = (chapter ~= nil) and epub.chapter(chapter) or nil
  if n == nil then
    epub.close()
    console.print('reader: this book has no chapter this reader can read')
    return false
  end

  bytes = n
  title = chapter:match('([^/]+)%.%w+$') or chapter
  from = (where ~= nil) and where.byte or 0
  if from >= bytes then from = 0 end
  remember(one.path, chapter, from)

  if not build_page() then
    epub.close()
    return false
  end

  return true
end

local function show_shelf()
  give_back()
  if book ~= nil then
    epub.close()
    book = nil
  end

  ui.title('Library')
  books = shelf_of()

  if #books == 0 then
    local empty = ui.label(view.w - 40, 120)
    if empty ~= nil then
      empty.text = 'No book on the flash or the card. Put one in /epub.'
      empty.scale = 3
      empty.color = 0
      empty.background = 15
      empty:place(20, 20)
    end
    screen = empty
    ui.present('gc16')
    return
  end

  local grid = ui.icon_grid(view.w - 40, view.h - 60)
  if grid == nil then return end

  local items = {}
  for i, one in ipairs(books) do items[i] = { label = one.title, mark = string.format('%02d', i) } end

  grid.items = items
  grid.columns = 2
  grid.scale = 3
  grid:place(20, 20)
  grid.on_pick = function(self, index)
    if books[index] ~= nil then open_book(books[index]) end
  end

  screen = grid
  ui.present('gc16')
end

-- **The beginning of the chapter**, which is what a finger going up asks for: the first byte, and the position is
-- written down as any page (docs/46, C3).
local function first_page()
  if screen == nil or book == nil or from == 0 then return end

  from = 0
  forget_page()
  present_page()
end

-- **A page is turned with a finger or with a key** (0.135.0), and the two say the same three things: a drag to the
-- left is the page after this one, a drag to the right the one before, and a drag up the beginning of the chapter.
-- The length is a hand's, and the way that moves more is the way the gesture is: a diagonal is not two pages.
--
-- The gestures are shapes of the flow of fingers that the system makes (`touch.gesture`, docs/30) and not something
-- the chip knows. The keys of the board reach `on_button` **because this page has no focusable control**: if it had
-- one, the system would take the dial for it (docs/30, docs/90).
local kSwipe = 60

local function on_gesture()
  if screen == nil or book == nil or touch == nil or touch.gesture == nil then return end

  local gesture = touch.gesture()
  if gesture == nil or gesture.kind ~= 'drag' then return end

  local dx = gesture.dx or 0
  local dy = gesture.dy or 0
  if math.abs(dx) >= math.abs(dy) then
    if dx <= -kSwipe then
      turn_forward()
    elseif dx >= kSwipe then
      turn_back()
    end
  elseif dy <= -kSwipe then
    first_page()
  end
end

-- **The system gave the app another view** (0.136.0, docs/46 D1): the whole glass when it is asked for, and the glass
-- under the bar when three taps give it back —which the system hears and the app does not—. The page of a book is drawn
-- again in the new box, on the byte it was at: a resize is not a page turn and the position does not move.
function on_resize(width, height)
  use_view(width, height)
  if book ~= nil then
    build_page()
  else
    show_shelf()
  end
end

-- **The keys of the board**: the one in the middle and the one below go forward, the one above goes back, which is
-- the same order as the fingers. On a board with one key —the ATOM— that key is the middle one and it turns the page.
function on_button(kind, held_ms, key)
  if kind ~= 'press' or screen == nil or book == nil then return end

  if key == 1 then
    turn_back()
  elseif key == 0 or key == 2 then
    turn_forward()
  end
end

-- The book of the manifest, when the shell opened one with `open`: the app starts on its page and not on the shelf
-- (docs/46, B10).
local opened = app.args and app.args.path
if opened ~= nil and opened ~= '' then
  if not open_book({ path = opened, title = opened:match('([^/]+)%.epub$') or opened }) then show_shelf() end
else
  show_shelf()
end

while true do
  on_gesture()
  ui.present()
  sys.delay(50):await()
end
