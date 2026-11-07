-- reader: the app of a book — the shelf, the page, the foot, the menu and where you were (docs/46, C1..C13).
--
-- **The app does not lay out anything.** The engine behind a page is the system's —the zip, the tokenizer, the
-- paginator and the face— and this file is what a person touches: which book, which page, and the number that says
-- how far in they are (docs/46, §1). Everything else here is the shelf, the menu, the foot and the file of positions.
--
-- **The position of a book is a chapter and a byte**, not a page number, because a page is as many words as fit a
-- box of a machine: `epub.page` says the byte the next page starts at and the app keeps it. Going forward is that
-- byte; going back is walking the chapter from zero with `measure`, which lays the pages out and paints nothing
-- (docs/46, B4) — a chapter of a book is a few dozen pages and walking them is cheaper than keeping a table that
-- the screen would have to be told about anyway.
--
-- **The frame of the page is the frame of the paper's reader** (0.140.0): a finger on the right half of the page is the
-- one after this one and on the left half the one before (C3), the book says it is being opened in a turn of its own
-- (C8), what the page will cost is decided from the tones the two pages have (C10), a book that will not open is a
-- notice on the shelf and not a line of the console (C12), and the words and the titles have a face each (C13).
--
-- What it does not do, said here so nobody looks for it: no cover and no thumbnails (C11 needs a cache of images and a
-- `save` of a surface), and the chapter is the first one that looks like a chapter until the OPF is read in Lua (C2).

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
--
-- **The foot is the one of the reader of the paper** (0.138.0, docs/46 C5): **84 rows** —a line across at the top, the
-- chapter and how far in on the left with the end of the chapter on the right, and under them the strip of the pages of
-- the chapter— which is the row the text of the book never touches.
local kFoot = 84
local kFootLine = 2
local kFootText = 8
local kFootRow = 30
local kFootEnd = 200
local kStripTop = 44
local kStripH = 40
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

-- **The two faces of the book** (0.140.0, docs/46 C13): the serif of the words and the sans of the titles, both on the
-- disk and both with the letters of Latin-1. A disk without the sans reads the titles in the serif, and a disk with
-- neither reads the book in the face of the system — the page is the same page with the letters this machine has. The
-- step is four, which is the thirty-six pixels of body of the paper project.
local kBody = 4
local face = ui.load_font('/fonts/dejavuserif-latin1.ttf')
if face == nil then
  console.print('reader: no serif face on the disk, the system face reads the book')
end
local title_face = ui.load_font('/fonts/dejavusans-latin1.ttf')
if title_face == nil then title_face = face end

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
-- **Which chapter of the book is open, and how many the book has** (0.138.0, docs/46 C5): what the foot says in
-- `Chapter 3/12` and what the percentage is a share of. The list is the chapters this reader can see —an `.xhtml`,
-- `.html` or `.htm` that is not inside `META-INF`, in the order of the names—, which is the same answer the system gives
-- for `epub.first_chapter` until the OPF is read (docs/46, C2).
local chapter_at = 1
local chapter_total = 1
local bytes = 0
local from = 0
local next_at = 0
local ended = false
local foot = nil
local endmark = nil
local strip = nil

-- **The pages of a chapter, counted in the background** (0.138.0, docs/46 C5): `measure` lays a page out and paints
-- nothing, and the walk of a chapter of half a megabyte is more than one turn of the glass, so it is paid a few pages at
-- a time between two turns of the loop. `pages` is the byte each page starts at, in order, and it is also what going
-- back a page needs: the page before this one is the one before it in the table (docs/46, B4).
local kCountPerTurn = 4
local kMaxPages = 512
local pages = {}
local walked = 0
local counting = false
local count_at = 0
local count_w = -1
local count_h = -1
-- **The page with its ink and its paper swapped** (0.137.0, docs/46 C9): it is only the page of the book —not the shelf
-- and not the cover—, it is not saved anywhere, and it says the same words in the same places with the two tones the
-- other way round. The menu of the View has the switch that turns it (docs/46, C4).
local dark = false
-- **A page of text is two tones and a page with a plate is not** (0.140.0, docs/46 C10): the engine paints words, rules
-- and plates and nothing between them, so this flag is the whole answer about the levels of a page, and the turn of a
-- page reads it to choose between the two-level pass and the wash that leaves no ghost (docs/50).
local two_tone = false

-- The two tones of a page, which are the two tones of the menu over it: ink and paper, or paper and ink.
local function ink_level() return dark and 15 or 0 end
local function paper_level() return dark and 0 or 15 end

-- **How far in the book the page is**, which is what the foot says (docs/46, C5): the chapters that are behind us and
-- the share of this one that is read, over the chapters the book has — the same sum the reader of the paper makes, and
-- the reason the number is of the book and not of the chapter.
local function percent()
  if bytes <= 0 or chapter_total <= 0 then return 0 end
  local p = math.floor(100 * ((chapter_at - 1) + from / bytes) / chapter_total + 0.5)
  if p < 0 then p = 0 end
  if p > 100 then p = 100 end
  return p
end

-- **The walk of the chapter, a few pages at a time** (0.138.0, docs/46 C5): from zero to the end of the chapter, and
-- the table of what each page starts at. False when there is no chapter to count, and `counting` says whether it is
-- still going, which is what the foot reads to say a number or a percentage.
local function start_count(force)
  if not force and count_w == kPageW and count_h == kPageH and walked > 0 then return end
  pages = {}
  walked = 0
  count_at = 0
  count_w = kPageW
  count_h = kPageH
  counting = (book ~= nil and screen ~= nil and bytes > 0)
end

-- **A few pages of the walk per turn**, and the answer says whether it found any: the foot is written again only when
-- the count has landed, because a foot repainted every turn is ink spent on a number that has not changed (docs/50).
local function count_some()
  if not counting or screen == nil then return false end
  for i = 1, kCountPerTurn do
    local nxt, done = epub.page(screen, count_at, {
      face = face,
      title_face = title_face,
      scale = kBody,
      title_scale = kBody + 1,
      small_scale = kBody - 1,
      measure = true,
    })
    if nxt == nil then
      counting = false
      return true
    end
    walked = walked + 1
    pages[walked] = count_at
    if done or nxt <= count_at or walked >= kMaxPages then
      counting = false
      return true
    end
    count_at = nxt
  end
  return true
end

-- **Which page of the chapter a byte is on**, from the table of the count: 0 while the walk has not reached it.
local function page_index(at)
  local found = 0
  for i = 1, walked do
    if pages[i] > at then break end
    found = i
  end
  return found
end

-- **The strip of the pages of the chapter, drawn as the reader of the paper draws it** (0.138.0, docs/46 C5): a ball of
-- twelve pixels for each page —solid for the one that is read and a ring for the rest— on a rod of one pixel, a window of
-- fourteen, and **three balls that get smaller at an end the window does not reach**, which is how it says that there are
-- many more and it cannot draw them. It is drawn in a canvas of the width of the page, so the whole band is one layer and
-- one region of damage (docs/30, docs/90).
local kDot = 12
local kDotGap = 6
local kWindow = 14
local kTrail = { 8, 6, 4 }

local function draw_strip()
  if strip == nil then return end
  strip:clear(paper_level(), 'du')
  local total = walked
  local here = page_index(from)
  if total < 2 or here < 1 then return end

  local count = math.min(total, kWindow)
  local first = here - count // 2
  if first < 1 then first = 1 end
  if first + count - 1 > total then first = total - count + 1 end

  local width = count * kDot + (count - 1) * kDotGap
  local left = (kPageW - width) // 2
  local y = 10
  local cy = y + kDot // 2
  local ink = ink_level()

  -- **The rod goes first and the balls on top of it**: what is seen of it is the air between two balls (docs/90).
  strip:rect(left + kDot // 2, cy - 1, width - kDot, 1, ink)
  strip:rect(left + kDot // 2, cy, width - kDot, 1, ink)

  local x = left
  for k = first, first + count - 1 do
    if k == here then
      strip:disc(x + kDot // 2, cy, kDot // 2, ink)
    else
      strip:circle(x + kDot // 2, cy, kDot // 2, ink)
    end
    x = x + kDot + kDotGap
  end

  -- **The three that say there are more**: smaller and smaller, on the side the window does not reach.
  if first > 1 then
    local tx = left - 8
    for k = 1, #kTrail do
      local s = kTrail[k]
      tx = tx - s
      strip:disc(tx + s // 2, y + (kDot - s) // 2 + s // 2, s // 2, ink)
      tx = tx - 4
    end
  end
  if first + count - 1 < total or counting then
    local tx = left + width + 8
    for k = 1, #kTrail do
      local s = kTrail[k]
      strip:disc(tx + s // 2, y + (kDot - s) // 2 + s // 2, s // 2, ink)
      tx = tx + s + 4
    end
  end
end

-- **What the foot says**: the chapter that is open and how many the book has, how far in the book we are, and —on the
-- right— the end of the chapter when the page is the last one (docs/46, C5).
local function say_foot()
  if foot == nil then return end
  -- **The two words of the separator are a hyphen and not the middle dot of the paper**: the face of the system of this
  -- build is 95 glyphs of ASCII and the `·` is not one of them (docs/46, D3). A Latin-1 face of the system is a decision
  -- of the owner and it is written down there.
  foot.text = string.format('Chapter %d/%d - %d%%', chapter_at, chapter_total, percent())
  if endmark ~= nil then
    if ended then
      endmark.text = (chapter_at >= chapter_total) and 'the end' or 'chapter end'
    else
      endmark.text = ''
    end
  end
end

-- **What went wrong is a notice of the system** (0.140.0, docs/46 C12): a book that will not open, a chapter with no
-- words this reader can read, a page that cannot be composed. The band says it on the glass and the card of the notice
-- opens the whole sentence when it is touched, because a sentence cut at the edge of a card is a sentence nobody read.
-- Nothing of this is saved: the band of an app lives in the app's memory and goes with the page (docs/30, docs/90).
local notice = nil
local notice_card = nil
-- **A band whose last card went is released in the turn after** (docs/90): a band with no card is a control that still
-- takes the dial, and a page that stops turning with the keys under a notice nobody can see is a bug of the notice.
local notice_gone = false

local function release_notice()
  notice_gone = false
  if notice_card ~= nil then
    notice_card:release()
    notice_card = nil
  end
  if notice ~= nil then
    notice:release()
    notice = nil
  end
end

local function show_error(headline, text)
  if notice_card ~= nil then notice_card:release() end
  notice_card = ui.dialog(math.min(kPageW - 40, 500), math.min(320, view.h - 80))
  if notice_card == nil then return end

  notice_card.title = headline
  notice_card.text = text
  notice_card.accept = 'Close'
  -- **The one door is the good one**: a card that says what happened has nothing to cancel, and an empty `cancel` is
  -- how a card is left with a single door across its width (docs/30, docs/90).
  notice_card.cancel = ''
  notice_card.emphasis = 2
  notice_card:place(20, 120)
  notice_card.on_accept = function(self)
    self:release()
    notice_card = nil
    ui.present('gc16')
  end
  notice_card.on_cancel = notice_card.on_accept
  ui.present('gc16')
end

local function put_a_notice(headline, text)
  release_notice()
  local band = math.min(240, view.h - 40)
  notice = ui.notification_bar(kPageW, band)
  if notice == nil then return end

  notice.items = { { title = headline, text = text } }
  notice.scale = 3
  notice:place(0, view.h - band)
  -- **The cross dismisses and the card opens the whole sentence** (docs/30, docs/90): the list is the app's and the
  -- control keeps none of it.
  notice.on_close = function(self, item)
    table.remove(self.items, item)
    self.items = self.items
    if #self.items == 0 then notice_gone = true end
    ui.present('gc16')
  end
  notice.on_pick = function(self, item)
    local one = self.items[item]
    show_error(headline, (one ~= nil and one.text) or text)
  end
  ui.present('gc16')
end

local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
  -- **The foot is the page's and it goes with it** (0.138.0): a label left behind would keep its words on the glass of the
  -- shelf for ever, because nothing paints over what is not drawn again (docs/30, docs/90).
  if foot ~= nil then foot:release() end
  foot = nil
  if endmark ~= nil then endmark:release() end
  endmark = nil
  if strip ~= nil then strip:release() end
  strip = nil
  pages = {}
  walked = 0
  counting = false
  count_w = -1
  count_h = -1
  -- **The page that says what the glass has on it goes with the page** (docs/46, C10): the next page is composed over
  -- whatever the shelf left, and a `du` asked from a flag of a book that is gone is a ghost.
  two_tone = false
  release_notice()
end

local function present_page(wash)
  if screen == nil or book == nil then return end

  -- **The paper of the page goes down first and the quality is decided later**: what is drawn below is written before the
  -- page says whether it carries a plate, and the present at the end is the one that chooses the pass (docs/46, C10).
  screen:clear(paper_level(), 'du')
  local nxt, done, used, picture = epub.page(screen, from, {
    face = face,
    title_face = title_face,
    scale = kBody,
    title_scale = kBody + 1,
    small_scale = kBody - 1,
    dark = dark,
  })
  if nxt == nil then
    -- **A page that cannot be composed is a notice and not a line of the console** (0.140.0, docs/46 C12): the person
    -- is looking at the glass, and the card of the notice has the whole sentence when it is touched.
    put_a_notice('This page cannot be composed', 'the book is open and this page is not: turn it or go to the shelf')
    return false
  end

  next_at = nxt
  ended = done
  foot.color = ink_level()
  foot.background = paper_level()
  say_foot()
  draw_strip()

  -- **Two pages of two tones are a two-level pass, and anything else is a wash** (docs/46, C10): a page with a plate has
  -- the grays of the picture, and the page that comes after one has the ghost of them, so either of the two takes the
  -- full pass (docs/50). **A page that is built again** —a resize, the menu folding, the dark page— takes the wash
  -- whatever it is made of: what was under it was a card, the bar or the same page in the other tones.
  local two = not picture
  local quality = (two and two_tone and not wash) and 'du' or 'gc16'
  two_tone = two
  ui.present(quality)

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
  if foot ~= nil then foot:release() end
  foot = nil
  if endmark ~= nil then endmark:release() end
  endmark = nil
  if strip ~= nil then strip:release() end
  strip = nil
  if book == nil then return false end

  screen = ui.canvas(kPageW, kPageH)
  if screen == nil then
    console.print('reader: there is no memory for a page')
    return false
  end
  screen:place(0, 0)
  if face ~= nil then screen:font(face) end

  -- **The paper of the foot and its line across** (docs/46, C5): the foot is the system's row and not the page's, so the
  -- app paints it —and on a dark page it is black like the page— before the words and the strip go on it.
  ui.rect(0, kPageH, kPageW, kFoot, paper_level(), 'du')
  ui.rect(16, kPageH + kFootLine, kPageW - 32, 1, ink_level(), 'du')

  -- **The chapter on the left and the end of it on the right**, on the first row of the foot.
  foot = ui.label(kPageW - 32 - kFootEnd, kFootRow)
  if foot ~= nil then
    foot.text = ''
    foot.scale = 2
    foot.align = 'left'
    foot.color = ink_level()
    foot.background = paper_level()
    foot:place(16, kPageH + kFootText)
  end

  endmark = ui.label(kFootEnd, kFootRow)
  if endmark ~= nil then
    endmark.text = ''
    endmark.scale = 2
    endmark.align = 'right'
    endmark.color = ink_level()
    endmark.background = paper_level()
    endmark:place(kPageW - 16 - kFootEnd, kPageH + kFootText)
  end

  -- **And the strip of the pages under them**, in a canvas of the width of the page so the whole band is one layer.
  strip = ui.canvas(kPageW, kStripH)
  if strip ~= nil then strip:place(0, kPageH + kStripTop) end

  -- **And the chapter is counted from here**, because a page is as many words as fit this box and the box has just been
  -- made (docs/46, C5).
  start_count(false)

  return present_page(true)
end

-- **The page after this one** is the byte the page said, and there is nothing to compute (docs/46, §1).
local function turn_forward()
  if ended or screen == nil or book == nil then return end

  from = next_at
  forget_page()
  present_page()
end

-- **The page before this one** is the one before it in the table of the count (0.138.0, docs/46 C5), and while the walk
-- has not reached the page we are on it is the last page that started before this byte, found by walking the chapter with
-- `measure`: the same arithmetic and not a pixel (docs/46, B4).
local function turn_back()
  if screen == nil or book == nil or from == 0 then return end

  local which = page_index(from)
  if which > 0 then
    from = (which > 1) and pages[which - 1] or 0
  else
    local walk = 0
    local last = 0
    while true do
      local nxt = epub.page(screen, walk, { face = face, scale = kBody, measure = true })
      if nxt == nil or nxt <= walk or nxt > from then break end
      last = walk
      walk = nxt
    end
    from = last
  end

  forget_page()
  present_page()
end

-- **The book of a shelf entry, opened** — and the ways it can refuse are a notice and not a line of the console
-- (0.140.0, docs/46 C12): the person stays on the shelf, the band says what happened and the card has the whole sentence.
local show_shelf

local function open_book(one)
  give_back()

  local ok, why = epub.open(one.path)
  if not ok then
    local said = tostring((why ~= nil and why.message) or 'the file is not a book this reader can open')
    book = nil
    show_shelf(said)
    return false
  end

  local where = positions()[one.path]
  local which = (where ~= nil) and where.chapter or epub.first_chapter()
  local n = (which ~= nil) and epub.chapter(which) or nil
  if n == nil then
    epub.close()
    book = nil
    show_shelf('the book has no chapter this reader can read')
    return false
  end

  book = one
  ui.title(one.title)

  chapter = which
  bytes = n
  title = chapter:match('([^/]+)%.%w+$') or chapter
  -- **Which chapter of the book this one is, and how many the book has** (0.138.0, docs/46 C5): the foot says
  -- `Chapter 3/12` and the percentage is a share of the book, so both numbers are of the chapters the reader can see —
  -- the `.xhtml`, `.html` and `.htm` of the book that are not inside `META-INF`, in the order of the names, which is the
  -- list `epub.first_chapter` walks until the OPF is read (docs/46, C2).
  chapter_at = 1
  chapter_total = 0
  for i = 1, epub.entries() do
    local name = epub.name(i)
    if name ~= nil and not name:find('^META%-INF/') and name:match('%.x?html?$') then
      chapter_total = chapter_total + 1
      if name == chapter then chapter_at = chapter_total end
    end
  end
  if chapter_total < 1 then chapter_total = 1 end

  from = (where ~= nil) and where.byte or 0
  if from >= bytes then from = 0 end
  remember(one.path, chapter, from)

  if not build_page() then
    epub.close()
    book = nil
    show_shelf('there is no memory for a page of this book')
    return false
  end

  return true
end

-- **The book opens in the turn after the one that asks for it** (0.140.0, docs/46 C8): the panel only starts travelling
-- when the callback that drew has finished, so the page that says which book is being opened has to be a turn of its own.
-- The app draws it and presents it here, and the loop below opens the book in the turn that comes.
local pending = nil

local function ask_for(one)
  give_back()
  ui.title(one.title)

  local page = ui.canvas(kPageW, kPageH)
  if page == nil then
    -- **A loading page is a courtesy and not the door**: a machine with no memory for it opens the book anyway.
    pending = one
    return
  end

  page:clear(15, 'gc16')
  page:text(20, 60, 'Opening the book...', 4, 0, 'du')
  page:text(20, 140, one.title, 3, 8, 'du')
  page:place(0, 0)
  screen = page
  pending = one
  ui.present('gc16')
end

-- **The shelf, and the sentence of what went wrong when there is one** (0.140.0, docs/46 C12): the notice is put after the
-- shelf is on the glass, so it is the last thing the person sees and the first thing they can touch.
function show_shelf(why)
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
    if why ~= nil then put_a_notice('The book would not open', why) end
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
    -- **A book is not opened from here** (docs/46, C8): the loading page goes up in this turn and the book opens in the
    -- next one, which is when the callback of this pick has already finished.
    if books[index] ~= nil then ask_for(books[index]) end
  end

  screen = grid
  ui.present('gc16')
  if why ~= nil then put_a_notice('The book would not open', why) end
end

-- **The beginning of the chapter**, which is what a finger going up asks for: the first byte, and the position is
-- written down as any page (docs/46, C3).
local function first_page()
  if screen == nil or book == nil or from == 0 then return end

  from = 0
  forget_page()
  present_page()
end

-- **The menu of the reader** (0.137.0, docs/46 C4): a row of words over the page and the card of the one that is
-- unfolded, and nothing of it is on the page until a person asks for it. That shape is what the page needs: a control
-- that is always there takes the dial, and then the keys stop turning the page (0.135.0). So the menu is made when it is
-- asked for and given back when it goes (docs/30, docs/90).
--
-- The way in is a **long press** on the page. The ways out are the same long press, a tap outside the row of words and a
-- swipe: a menu that stays open behind a finger that has moved on is a menu a person has to dismiss twice (docs/90).
local kMenuBar = 52
local menu = nil

local function release_menu()
  if menu ~= nil then
    menu:release()
    menu = nil
  end
end

-- **What the menus say**: the Book has the Shelf —the Contents will be here when the book gives its real chapters
-- (C2, C14)— and the View has the whole glass and the dark page, which are the two states of this page. An entry with
-- `checked` carries its box, even when it is off (docs/30, docs/90).
local function menu_items()
  return {
    { title = 'Book', items = { 'Shelf' } },
    { title = 'View', items = {
      { label = 'Full screen', checked = ui.full_screen() },
      { label = 'Dark page', checked = dark },
    } },
  }
end

local function close_menu()
  if menu == nil then return end
  release_menu()
  -- **The page under the card is drawn again**: the card covered a part of it and a present paints only what changed
  -- (docs/30).
  if book ~= nil then
    build_page()
  else
    show_shelf()
  end
end

-- **A choice of the menu**, said by the system with the card already folded (docs/90): the title and the entry, counted
-- from one and in the order of `menu_items`.
local function on_menu_pick(title, entry)
  release_menu()
  if title == 1 then
    show_shelf()
  elseif entry == 1 then
    -- **The system answers with `on_resize`**, which builds the page in the new box: nothing else is owed here.
    ui.full_screen(not ui.full_screen())
  else
    dark = not dark
    build_page()
  end
end

local function open_menu()
  if menu ~= nil or book == nil then return end

  local bar = ui.menu_bar(kPageW, kPageH)
  if bar == nil then return end

  bar.items = menu_items()
  -- **The View is the menu that is down**, because it is the one with the two states of this page; the Book is a word a
  -- finger unfolds and the dial does not leave the entry it is on (docs/90).
  bar.open = 2
  bar.index = 1
  bar.bar_height = kMenuBar
  bar.color = ink_level()
  bar.background = paper_level()
  bar.dim = 8
  bar:place(0, 0)
  bar.on_pick = function(self, which_title, which_entry)
    on_menu_pick(which_title, which_entry)
  end
  menu = bar
  ui.present('gc16')
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
  if gesture == nil then return end

  -- **With a menu up, the finger is the menu's**: a tap below the row of words and a swipe put it away, and neither
  -- turns the page behind it. The tap that chose an entry was already answered by the system and the menu is gone by the
  -- time this one arrives (docs/90).
  if menu ~= nil then
    local kind = gesture.kind
    if kind == 'long' or kind == 'drag' then
      close_menu()
    elseif kind == 'tap' and (gesture.y or 0) >= kMenuBar then
      close_menu()
    end
    return
  end

  -- **And with a notice up the finger is the notice's**: the band and its card are the last word on the glass, and a tap
  -- that opens or dismisses them is not a page turn (0.140.0, docs/46 C12).
  if notice ~= nil or notice_card ~= nil then return end

  -- **A long press is the menu**, which is the one gesture that does not fight a page turn: a drag is a page and a tap is
  -- a page too (docs/46, C3 and C4).
  if gesture.kind == 'long' then
    open_menu()
    return
  end

  -- **A tap turns the page the way a phone does** (0.140.0, docs/46 C3): the right half of the page is the one after
  -- this one and the left half the one before. **The foot is not the page**, so a tap on it is nothing —a person puts a
  -- finger there to read the number and not to move—, and the middle line is the right half, which is the way forward.
  if gesture.kind == 'tap' then
    local y = gesture.y or 0
    if y < kPageH then
      if (gesture.x or 0) < kPageW // 2 then
        turn_back()
      else
        turn_forward()
      end
    end
    return
  end

  if gesture.kind ~= 'drag' then return end

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
  -- **A new view takes the controls with it** (docs/90): the menu of the page before is a canvas nobody has any more.
  release_menu()
  use_view(width, height)
  if pending ~= nil then
    -- **A book that is still being opened keeps its page** (docs/46, C8): the loading page is drawn again in the new box
    -- and the book opens in the turn that comes, which is where it was going anyway.
    ask_for(pending)
  elseif book ~= nil then
    build_page()
  else
    show_shelf()
  end
end

-- **The keys of the board**: the one in the middle and the one below go forward, the one above goes back, which is
-- the same order as the fingers. On a board with one key —the ATOM— that key is the middle one and it turns the page.
function on_button(kind, held_ms, key)
  if kind ~= 'press' or screen == nil or book == nil then return end
  -- **A key does not turn the page behind a menu**: while the card is up the dial is the system's, and a page that moved
  -- under a menu would be a page nobody asked for (docs/46, C4). A notice is a page of its own in the same way (C12).
  if menu ~= nil or notice ~= nil or notice_card ~= nil then return end

  if key == 1 then
    turn_back()
  elseif key == 0 or key == 2 then
    turn_forward()
  end
end

-- The book of the manifest, when the shell opened one with `open`: the app starts on its page and not on the shelf
-- (docs/46, B10). It opens through the loading page like a book picked on the shelf (docs/46, C8).
local opened = app.args and app.args.path
if opened ~= nil and opened ~= '' then
  ask_for({ path = opened, title = opened:match('([^/]+)%.epub$') or opened })
else
  show_shelf()
end

while true do
  -- **The book asked for in the turn before opens in this one** (docs/46, C8): the loading page was presented on the
  -- glass before the callback that asked ended, and this is the next turn.
  if pending ~= nil then
    local one = pending
    pending = nil
    open_book(one)
  end

  -- **And a notice whose last card was dismissed goes now**: the turn that heard the cross is not the one that takes
  -- the control away (docs/90).
  if notice_gone then
    release_notice()
    ui.present('gc16')
  end

  on_gesture()
  -- **The count of the chapter takes a few pages of every turn** (docs/46, C5), and the strip is drawn again when it
  -- has landed: a foot painted every turn would be ink spent on a number that has not changed (docs/50).
  if count_some() and not counting then draw_strip() end
  ui.present()
  sys.delay(50):await()
end
