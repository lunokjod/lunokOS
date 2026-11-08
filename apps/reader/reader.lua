-- reader: the app of a book — the shelf, the page, the foot, the menu and where you were (docs/46, C1..C16).
--
-- **The app does not lay out anything.** The engine behind a page is the system's —the zip, the tokenizer, the
-- paginator and the face— and this file is what a person touches: which book, which page, and the number that says
-- how far in they are (docs/46, §1). Everything else here is the shelf, the menu, the foot and the file of positions.
--
-- **It is four files since 0.149.0** (docs/46, D6): `library` is where the books are and where each one was left,
-- `chapters` reads the `container.xml`, the OPF and the index of a book, `notice` is the band and the card that say
-- what went wrong, and this one is the page, its count, its foot and the hands that turn it. They are read with
-- `require` from the folder the app was installed in.
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
-- What it does not do, said here so nobody looks for it: **the cells of the shelf are words and not pictures** —the
-- cache of a book is written beside it since 0.146.0 (C11) and no cell reads it back yet (C16); the cache is for the app
-- that will draw it—.

local caps = sys.caps()
-- **The app is more than one file** (0.149.0, docs/46 D6): `require` reads these from the folder the app was installed
-- in and runs each of them once. `library` is where the books are and where each one was left, `chapters` reads the OPF
-- and the index of a book, and `notice` is the band and the card that say what went wrong. What is left here is the
-- page, its count, its foot and the hands that turn it.
local library = require('library')
local chapters = require('chapters')
local notice = require('notice')
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

-- **And the notice is told where the glass is** (0.149.0): it draws the band at the bottom of the view and the card in
-- the middle of it, and the table `view` is the same one `use_view` changes, so a view the system gives later reaches the
-- module without another call (docs/46, D1).
notice.set_view(view)

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
-- **The face of a book and the face of the disk** (0.150.0, docs/46 D6): the serif of the disk is what this reader reads
-- with until a book carries its own, and the face of a book takes one of the three slots of the app while it is open and
-- goes back when the book does. `disk_face` is kept so that closing a book gives the letter of the disk back.
local disk_face = face
local book_face = nil


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
-- **The foot of the shelf** (0.147.0, docs/46 C1): the same band and the same two labels as the foot of a page, so the
-- shelf and a book are one machine and not two. They are released with the glass, like the page's foot.
local shelf_foot = nil
local shelf_help = nil
-- **The spine of the open book and its index** (0.143.0, docs/46 C2 and C14): the chapters in the order they are read
-- and the entries of the book's own index —a label and the chapter it opens—. A book of one chapter declares no index
-- and one is not made up for it.
local chapter_list = {}
local toc = {}
-- **The cover of the book and the fact of being on it** (0.144.0, docs/46 C7): the first page of the first chapter
-- starts at byte zero, so «I am on the cover» is a state and not a byte, and it is saved with the position.
local cover = nil
local cover_mode = false
-- **The index on the glass**: the list of its rows while it is up and the title above it. It is a screen of its own,
-- so the page is given back while it is there and built again when it goes (docs/30, docs/90).
local index_list = nil
local index_head = nil
-- **The finger that chose a row belongs to the row** (docs/46, C14): the system answers the press and the same finger
-- arrives once more as a `tap`, and a page turned by it would be a page nobody asked for. This is the moment until
-- which a tap is that one, and the window is the one the finger takes to be counted as a tap (docs/30).
local swallow_tap = 0

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
-- **The keys of the board are the page's only after the centre enters it** (0.141.0, docs/46 C3): the gate a slider
-- has (`ui.slider`: the arrows walk the controls until a person says «enter» with the middle key), which is what the
-- owner asked for with the page in his hands. Here the page is the only thing on the glass, so before the centre the
-- arrows walk nothing at all; the right of the foot says `keys` while the page is entered, because a mode nobody can
-- see is a mode that looks like a key that does not work.
local page_mode = false

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
  -- **The cover has no strip of pages**: it is a picture of the book and not one of its pages (docs/46, C7).
  if cover_mode then return end
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
    if page_mode then
      -- **The page is entered and the arrows are its own** (docs/46, C3): the right of the foot says what they do while
      -- it lasts, and the end of the chapter comes back when the mode goes.
      endmark.text = 'keys'
    elseif ended then
      endmark.text = (chapter_at >= chapter_total) and 'the end' or 'chapter end'
    else
      endmark.text = ''
    end
  end
end


-- **The page and its foot go back together** (docs/30): a canvas, two labels and the strip of the balls, which are
-- the four things a page of a book is made of here. The count of the chapter stays: the index gives the page back and
-- takes it again, and recounting a chapter for that would be walking it for nothing.
local function release_page()
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
  -- **And the foot of the shelf, when the glass was the shelf** (0.147.0): a label left behind keeps its words on the
  -- glass for ever, because nothing paints over what is not drawn again (docs/30, docs/90).
  if shelf_foot ~= nil then shelf_foot:release() end
  shelf_foot = nil
  if shelf_help ~= nil then shelf_help:release() end
  shelf_help = nil
end

-- **The index is a screen and it goes back with its rows** (docs/90).
local function release_index()
  if index_list ~= nil then index_list:release() end
  index_list = nil
  if index_head ~= nil then index_head:release() end
  index_head = nil
end

local function give_back()
  release_page()
  release_index()
  chapter_list = {}
  toc = {}
  pages = {}
  walked = 0
  counting = false
  count_w = -1
  count_h = -1
  -- **The page that says what the glass has on it goes with the page** (docs/46, C10): the next page is composed over
  -- whatever the shelf left, and a `du` asked from a flag of a book that is gone is a ghost.
  two_tone = false
  -- **And the keys are nobody's until the centre says so again** (docs/46, C3), and the cover is the book's (C7).
  page_mode = false
  cover = nil
  cover_mode = false
  notice.release()
  -- **And the face of the book goes back with it** (0.150.0, docs/46 D6): a face is one of the three slots of an app, and
  -- the letter of the disk is what the shelf and the next book start with.
  if book_face ~= nil then
    book_face:release()
    book_face = nil
  end
  face = disk_face
end

local function present_page(wash, silent)
  if screen == nil or book == nil then return end

  -- **A page on the glass is not the cover any more** (docs/46, C7).
  cover_mode = false

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
    notice.put('This page cannot be composed', 'the book is open and this page is not: turn it or go to the shelf')
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
  if not silent then ui.present(quality) end

  return true
end

-- **The cover** (0.144.0, docs/46 C7): it is a picture of the book and a state of the reader, it sits before the first
-- page of the first chapter and nowhere else, and it goes with the position. **A picture keeps its paper** — it is not
-- inverted on the dark page, because a negative cover is not a cover (C9) — and the foot says the one thing a person
-- has to know there: the page is to the right.
-- **The position is saved and `show_cover` does it** (docs/46, C6 and C7): `forget_page` is written below it and a
-- `local function` only exists from its own line, so the cover called a global that was not there (the suite found
-- it on 8/11: `attempt to call a nil value (global 'forget_page')`).
local forget_page

local function show_cover()
  if screen == nil or book == nil or cover == nil then return end

  cover_mode = true
  screen:clear(15, 'gc16')
  if not epub.picture(screen, cover) then console.print('reader: the cover cannot be read') end

  foot.color = 0
  foot.background = 15
  foot.text = 'Cover - right to read'
  if endmark ~= nil then endmark.text = '' end
  if strip ~= nil then strip:clear(15, 'du') end

  -- **A picture is not two tones**: the page that comes after it takes the wash (docs/46, C10).
  two_tone = false
  forget_page()
  ui.present('gc16')
end

function forget_page()
  if chapter ~= nil and book ~= nil then library.remember(book.path, chapter, from, cover_mode) end
end

-- **The page of the book, built again when the view changes size** (0.136.0, docs/46 D1): a canvas of the new size, the
-- foot under it, and the page drawn in it. The system makes a new view and the objects of before point at a canvas
-- nobody has any more, so this is a build and not a move (docs/30, docs/90).
local function build_page(restart, silent)
  release_page()
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
  -- made (docs/46, C5). A chapter the index opens is counted from zero even when the box is the same (docs/46, C14).
  start_count(restart == true)

  return present_page(true, silent)
end

-- **The glass the book is on**: its page at the byte it is at, or the cover when the cover is where the reader is
-- (0.144.0, docs/46 C7). Every path that gives the glass back to the book comes through here, so a menu closed over a
-- cover leaves a cover and not the first page of the chapter.
local function build_the_book()
  local was_cover = cover_mode and cover ~= nil
  build_page(false, was_cover)
  if was_cover then show_cover() end
end

-- **The page after this one** is the byte the page said, and there is nothing to compute (docs/46, §1).
local function turn_forward()
  if screen == nil or book == nil then return end

  -- **From the cover the next page is the first of the book** (docs/46, C7), and the cover was a picture: the page takes
  -- the wash (C10).
  if cover_mode then
    cover_mode = false
    forget_page()
    present_page(true)
    return
  end

  if ended then return end

  from = next_at
  forget_page()
  present_page()
end

-- **The page before this one** is the one before it in the table of the count (0.138.0, docs/46 C5), and while the walk
-- has not reached the page we are on it is the last page that started before this byte, found by walking the chapter with
-- `measure`: the same arithmetic and not a pixel (docs/46, B4).
local function turn_back()
  if screen == nil or book == nil then return end

  -- **The cover sits before the first page of the first chapter and only there** (0.144.0, docs/46 C7): from the first
  -- byte of the first chapter, back is the cover.
  if not cover_mode and from == 0 and chapter_at <= 1 and cover ~= nil then
    show_cover()
    return
  end

  if cover_mode or from == 0 then return end

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

  local list, book_title, book_toc, toc_from, book_cover, book_fonts = chapters.chapters_of_book()
  local where = library.positions()[one.path]
  local which = (where ~= nil) and where.chapter or list[1]
  local n = (which ~= nil) and epub.chapter(which) or nil
  if n == nil and list[1] ~= nil and which ~= list[1] then
    -- **A chapter the book does not carry any more** —the file was replaced under a saved position—: the book opens
    -- at its first chapter and the position of this one starts again (docs/46, C6).
    which = list[1]
    n = epub.chapter(which)
  end
  if n == nil then
    epub.close()
    book = nil
    show_shelf('the book has no chapter this reader can read')
    return false
  end

  book = one
  ui.title(book_title or one.title)

  chapter = which
  bytes = n
  title = chapter:match('([^/]+)%.%w+$') or chapter
  -- **Which chapter of the book is open, and how many the book has** (0.142.0, docs/46 C2 and C5): the spine of the
  -- OPF is the order of the book, and the names sorted are what a book without an OPF falls back to.
  chapter_at = 1
  for i = 1, #list do
    if list[i] == which then chapter_at = i end
  end
  chapter_total = (#list > 0) and #list or 1

  chapter_list = list
  toc = book_toc
  cover = book_cover
  -- **The cache of the book, beside the book** (0.146.0, docs/46 C11): the cover scaled to 64 pixels and written as an
  -- `ICS1` —a hidden `.la-vaca-cega.epub.cache.ics` beside the `.epub`— so that whoever lists the folder has the face of
  -- the book without opening it. It is asked once when the book opens and not at every page, and it is guarded: a cache
  -- is a courtesy beside a book and not the book.
  if cover ~= nil then pcall(epub.thumbnail, cover, 64) end
  -- **The face the book carries inside itself** (0.150.0, docs/46 D6): the first font its manifest declares is the body.
  -- A book with two is a book whose titles have their own letter, and the three slots of an app are three: the disk keeps
  -- two and the book takes the third. If the face cannot be read, the serif of the disk reads the book and the log says
  -- which of the two it was.
  if #book_fonts > 0 then
    local got, made = pcall(epub.font, book_fonts[1])
    if got and made ~= nil then
      book_face = made
      face = book_face
      console.print('reader: the face of the book is ' .. book_fonts[1])
    else
      console.print('reader: the face of the book cannot be read')
    end
  end

  from = (where ~= nil) and where.byte or 0
  if from >= bytes then from = 0 end
  -- **The cover is a state that is saved with the position** (docs/46, C7): a book opened where it was left on its
  -- cover is on its cover again.
  local on_cover = (where ~= nil) and where.cover == true
  library.remember(one.path, chapter, from, on_cover)

  -- **What the book is, said in the log** (docs/46, C2, C7 and C14): the title of its OPF, how many chapters the spine
  -- says, where its index came from and whether it has a cover — the three things a reader that had to fall back to the
  -- names cannot say.
  console.print(string.format('reader: %s, %d chapter(s), index %d (%s), cover %s', book_title or one.title,
                              chapter_total, #toc, toc_from, cover and 'yes' or 'no'))

  if not build_page(false, on_cover) then
    epub.close()
    book = nil
    show_shelf('there is no memory for a page of this book')
    return false
  end

  -- **And it opens where it was left**: a book saved on its cover is on its cover, and one page of it is drawn first
  -- without a present, so the glass goes from the shelf to the cover and not through the first page (docs/46, C7).
  if on_cover then show_cover() end

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

-- **The foot of the shelf** (0.147.0, docs/46 C1): the band of 84 rows of a page of a book —the paper, the line across
-- and one row of words— with the books it has on the left and what the keys do on the right. The strip of balls of the
-- paper's shelf is **not** here: the grid of cells already says which page its cells are on at the bottom of its own box,
-- and the system decided a grid's footer replaces the row of dots because two counters of one number on one glass is one
-- too many (docs/30, docs/90). The count is the shelf's because the grid cannot say it: it counts pages of cells.
local function draw_shelf_foot(count)
  ui.rect(0, view.h - kFoot, view.w, kFoot, 15, 'du')
  ui.rect(16, view.h - kFoot + kFootLine, view.w - 32, 1, 0, 'du')

  shelf_foot = ui.label(view.w - 32 - kFootEnd, kFootRow)
  if shelf_foot ~= nil then
    shelf_foot.text = (count == 1) and '1 book' or string.format('%d books', count)
    shelf_foot.scale = 2
    shelf_foot.align = 'left'
    shelf_foot.color = 0
    shelf_foot.background = 15
    shelf_foot:place(16, view.h - kFoot + kFootText)
  end

  shelf_help = ui.label(kFootEnd, kFootRow)
  if shelf_help ~= nil then
    shelf_help.text = 'centre opens'
    shelf_help.scale = 2
    shelf_help.align = 'right'
    shelf_help.color = 0
    shelf_help.background = 15
    shelf_help:place(view.w - 16 - kFootEnd, view.h - kFoot + kFootText)
  end
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
  books = library.shelf_of()

  -- **The shelf is paper from edge to edge** (0.147.0): a book read in the dark leaves a black page on the glass, and the
  -- margins around the grid and the air above the foot are rows that neither the grid nor the foot paints — without this
  -- line they keep the ink of the page that was being read (docs/46, C9). **And it is a wash and not a `du`** (0.151.2):
  -- the whole glass in the two-level waveform is what the controller of the Paper does not take, and the kernel turns a
  -- `du` that big into a `gc16` anyway —saying it here is saying which of the two the app meant (docs/50).
  ui.rect(0, 0, view.w, view.h, 15, 'gc16')

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
    if why ~= nil then notice.put('The book would not open', why) end
    return
  end

  local grid = ui.icon_grid(view.w - 40, view.h - 20 - kFoot - 8)
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

  -- **The foot goes under the cells and not on them** (0.147.0, docs/46 C1): the grid keeps its own footer —`2 / 3` and
  -- the two triangles of the pages— in the air the band leaves it.
  draw_shelf_foot(#books)

  screen = grid
  ui.present('gc16')
  if why ~= nil then notice.put('The book would not open', why) end
end

-- **A chapter of the open book, from a byte** (0.143.0, docs/46 C14): the index lands here, and it is the same three
-- things opening a book does — inflate the chapter, remember where we are and build the page again— with the count of
-- the new chapter starting from zero, because the pages of the old one are not its pages.
local function open_chapter(href, byte)
  local n = epub.chapter(href)
  if n == nil then return false end

  chapter = href
  bytes = n
  from = (byte ~= nil and byte > 0 and byte < n) and byte or 0
  cover_mode = false
  chapter_at = 1
  for i = 1, #chapter_list do
    if chapter_list[i] == href then chapter_at = i end
  end
  library.remember(book.path, chapter, from)

  return build_page(true)
end

-- **A row of the index is a chapter**: the finger that chose it belongs to it, the index goes, and the chapter is read
-- from its first byte (docs/46, C14). A chapter that cannot be read leaves the reader on the shelf with a notice.
local function go_to_chapter(at)
  local one = toc[at]
  if one == nil then return end

  swallow_tap = sys.time_ms() + 600
  release_index()
  if open_chapter(one.chapter, 0) then return end

  show_shelf('the chapter "' .. one.title .. '" cannot be read')
end

-- **A row of the pages is a page** (0.145.0, docs/46 C15): the reader goes to the byte that page starts at, which is the
-- table of the count of the chapter (C5), and it takes the wash because a jump of ten pages is not a turn.
local function go_to_page(at)
  local byte = pages[at]
  if byte == nil then return end

  swallow_tap = sys.time_ms() + 600
  release_index()
  cover_mode = false
  from = byte
  forget_page()
  build_page(false)
end

-- **A list on the glass** (docs/46, C14 and C15): the rows in a `ui.list` that pages itself, each row a control of its
-- own —the dial walks them and the centre opens one, and a finger opens the row it presses—. The row that is marked is
-- the one the reader is on and the list opens on its page. It is one function because the index of a book and the pages
-- of a chapter are the same screen with different rows.
local kIndexRow = 56
local kIndexHead = 64

local function open_list(head, rows, marked, pick)
  release_page()
  release_index()
  page_mode = false

  index_head = ui.label(view.w - 40, 48)
  if index_head ~= nil then
    index_head.text = head
    index_head.scale = 3
    index_head.color = 0
    index_head.background = 15
    index_head:place(20, 12)
  end

  local height = view.h - kIndexHead - 24
  index_list = ui.list(view.w - 40, height)
  if index_list == nil then
    build_the_book()
    return
  end

  local list = index_list
  list.row_height = kIndexRow
  list.separator = true
  list.scale = 3
  list.background = 15
  list.text = 0
  list.dim = 8

  -- **The list opens on the page of the row that is marked**, which is the one a person wants to see.
  local per_page = height // kIndexRow
  if per_page > 0 and marked > 0 then list.page = ((marked - 1) // per_page) + 1 end

  for i, one in ipairs(rows) do
    local entry = ui.list_entry(view.w - 40, kIndexRow)
    if entry ~= nil then
      entry.background = 15
      entry.border = 0
      entry.separator = true
      entry.marked = (i == marked)

      local words = ui.label(view.w - 64, kIndexRow - 8)
      if words ~= nil then
        words.text = one.title
        words.scale = 3
        words.align = 'left'
        words.color = 0
        words.background = 15
        words:place(12, 5)
        entry:add(words)
      end

      local at = i
      entry.on_click = function()
        pick(at)
      end
      list:add(entry)
    end
  end

  list:place(20, kIndexHead)
  ui.present('gc16')
end

-- **The index on the glass** (docs/46, C14): the rows of the book's own index, with the chapter that is open marked.
local function open_index()
  if book == nil then return end
  if #toc == 0 then
    -- **A book of one chapter has no index and none is invented** (docs/46, C14): the glass goes back to the book and a
    -- notice says why nothing happened.
    build_the_book()
    notice.put('This book has no index', 'it has one chapter, so there is nothing to choose')
    return
  end

  local marked = 0
  for i = 1, #toc do
    if toc[i].chapter == chapter then
      marked = i
      break
    end
  end

  open_list('Contents', toc, marked, go_to_chapter)
end

-- **And the pages of the chapter, to go to one by number** (0.145.0, docs/46 C15): a row for each page the count of the
-- chapter knows, opened at the page on the glass. **The number is walked and not typed**: a field that takes a number
-- brings the keyboard band, which is 581 rows of this glass, and walking a list with the two keys is quicker and does
-- not take the page away (docs/30).
local function open_pages()
  if book == nil then return end
  if walked < 2 then
    build_the_book()
    notice.put('There is no page to choose', 'this chapter has one page, or its count has not landed')
    return
  end

  local rows = {}
  for k = 1, walked do rows[k] = { title = 'Page ' .. k, at = k } end

  open_list('Pages', rows, page_index(from), go_to_page)
end

-- **And it goes back with a long press or a swipe**, the two fingers a menu also answers (docs/90): a tap is the
-- list's, and choosing a row is the other way out.
local function close_index()
  if index_list == nil then return end

  release_index()
  build_the_book()
end

-- **The beginning of the chapter**, which is what a finger going up asks for: the first byte, and the position is
-- written down as any page (docs/46, C3).
local function first_page()
  if screen == nil or book == nil then return end

  -- **From the cover, the beginning of the chapter is the first page**: the cover is already behind (docs/46, C7).
  if cover_mode then
    turn_forward()
    return
  end

  if from == 0 then return end

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
    -- **The Book is Shelf, Contents and Page** (docs/46, C1, C14 and C15): where the books are, which chapter and which
    -- page of it.
    { title = 'Book', items = { 'Shelf', 'Contents', 'Page' } },
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
    build_the_book()
  else
    show_shelf()
  end
end

-- **A choice of the menu**, said by the system with the card already folded (docs/90): the title and the entry, counted
-- from one and in the order of `menu_items`.
local function on_menu_pick(title, entry)
  release_menu()
  -- **The finger that chose an entry belongs to the entry** (docs/46, C14): the system answers the press and the same
  -- finger also arrives as a tap, and the page behind the card must not turn for it.
  swallow_tap = sys.time_ms() + 600
  if title == 1 then
    if entry == 1 then
      show_shelf()
    elseif entry == 2 then
      open_index()
    else
      open_pages()
    end
  elseif entry == 1 then
    -- **The system answers with `on_resize`**, which builds the page in the new box: nothing else is owed here.
    ui.full_screen(not ui.full_screen())
  else
    dark = not dark
    build_the_book()
  end
end

local function open_menu(which)
  if menu ~= nil or book == nil then return end

  -- **A menu takes the dial, so the page gives it back** (docs/30, docs/46 C3): a control the dial leaves is not entered
  -- any more, and a person who comes back to the page says «enter» again. The foot of the cover is not a page's.
  page_mode = false
  if not cover_mode then say_foot() end

  local bar = ui.menu_bar(kPageW, kPageH)
  if bar == nil then return end

  bar.items = menu_items()
  -- **The View is the menu that is down when a finger asks for the menu**, because it is the one with the two states of
  -- this page; the Book is a word a finger unfolds. **From the keys the Book is the one that is down** (0.145.0), because
  -- the arrows only walk the entries of the open title and a title is unfolded by the glass and not by the dial (docs/90).
  bar.open = which or 2
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
  if touch == nil or touch.gesture == nil then return end

  local gesture = touch.gesture()
  if gesture == nil then return end

  -- **The finger that chose a row belongs to the row** (docs/46, C14): the system answered the press and the same
  -- finger arrives here once more, and a page turned by it would be a page nobody asked for.
  if gesture.kind == 'tap' and sys.time_ms() < swallow_tap then return end

  -- **The index is a page of its own**: a long press or a swipe puts it away, and a tap is the list's (docs/46, C14).
  if index_list ~= nil then
    if gesture.kind == 'long' or gesture.kind == 'drag' then close_index() end
    return
  end

  if screen == nil or book == nil then return end

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
  if notice.up() then return end

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
  if index_list ~= nil then
    -- **The index is built again in the new box**: its rows are controls of a page that is gone, and a row a finger
    -- can press on a page that is gone is a row that opens nothing (docs/30).
    open_index()
  elseif pending ~= nil then
    -- **A book that is still being opened keeps its page** (docs/46, C8): the loading page is drawn again in the new box
    -- and the book opens in the turn that comes, which is where it was going anyway.
    ask_for(pending)
  elseif book ~= nil then
    -- **A resize is not a page and not a cover** (docs/46, D1 and C7): the byte does not move, and the glass that held
    -- a cover holds it again in the new box.
    build_the_book()
  else
    show_shelf()
  end
end

-- **The keys of the board**: the centre **enters the page** and leaves it, and while it is entered the two keys turn
-- it —up goes back and down goes forward, the same order as the fingers—. It is the gate of the slider (docs/30):
-- before the centre the keys are the walk of the dial, and this page has no control to walk, so they do nothing. A
-- notice or a menu is a page of its own and takes the keys and the mode with it (docs/46, C3, C4 and C12).
function on_button(kind, held_ms, key)
  -- **A book is what the keys are for**, and the lists of it take the keys themselves (docs/46, C14 and C15).
  if book == nil or index_list ~= nil then return end

  -- **A long press of the centre is the menu** (0.145.0, docs/46 C4), the way a long press on the glass is, and with
  -- the Book unfolded so the dial can walk it: a person at the keys has no finger to unfold a title with. The second
  -- long press folds it again.
  if kind == 'long' then
    if key ~= 0 then return end
    if menu ~= nil then
      close_menu()
    else
      open_menu(1)
    end
    return
  end

  if kind ~= 'press' or screen == nil then return end
  if menu ~= nil or notice.up() then return end

  if key == 0 then
    page_mode = not page_mode
    -- **The foot of the cover is not a page's**: the mode has no `keys` to say there (docs/46, C7).
    if not cover_mode then say_foot() end
    return
  end

  if not page_mode then return end

  if key == 1 then
    turn_back()
  elseif key == 2 then
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
  if notice.gone() then
    notice.release()
    ui.present('gc16')
  end

  on_gesture()
  -- **The count of the chapter takes a few pages of every turn** (docs/46, C5), and the strip is drawn again when it
  -- has landed: a foot painted every turn would be ink spent on a number that has not changed (docs/50).
  if count_some() and not counting then draw_strip() end
  ui.present()
  sys.delay(50):await()
end
