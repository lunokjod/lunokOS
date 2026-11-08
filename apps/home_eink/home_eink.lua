-- home_eink: **the launcher of the glass** (0.153.0, and 0.154.0 with the page that arrives before its pictures).
--
-- The apps of the disk, in a grid of pictures and names: a finger on a picture starts the app it belongs to. The list is
-- not read from `/apps` by this file —**the system reads it**—, because the door of the apps already puts the flash and
-- the card together and refuses a folder that is not an app (docs/40).
--
-- **The page comes first and the pictures after it, one app at a time** (0.154.0): `apps.list()` answers the names and
-- opens no manifest, so the grid is drawn at once with the name of each app and a **generic picture** —a box with its
-- first letter—, and then a queue asks `apps.info(id)` for one app per turn and puts the real name and the real picture
-- in the cell that is already there. A page that waited for every manifest and every picture before showing anything
-- arrived late, and one app whose picture cannot be read held up the rest (docs/30).
--
-- **A launcher does not list itself, and no launcher lists another.** This one knows its own name (`app.id`) and leaves
-- it out before anything is read; an app that says `"launcher": true` in its manifest is taken out when its manifest
-- arrives, and the page is laid out again without it. It is not a hidden app: `apps` lists it, `run home_eink` starts
-- it, and the board comes up inside it (`cfg home home_eink`).
--
-- **The pages are this launcher's own, and the band under the grid turns them** (0.154.1): a grid of the system puts the
-- cells that do not fit its frame out of sight, so a launcher that handed it every name would show the first page of them
-- and lose the rest. Here a page is a slice of the names —only its cells reach the grid— and the foot of the page holds
-- two arrows and the page it is, which is the one thing a person needs to know where they are and where they can go.
--
-- **A press is a wish and not a switch**: `app.run(id)` answers at once and the kernel does the change after the turn,
-- because the app that asks is the one the interpreter belongs to (docs/30, docs/90). The second argument is the way
-- back: with it the person returns to this page when the app they opened ends, which is the only way back on a board
-- whose single way out of an app is a key.
--
-- **The sizes are the grid's own** and not this file's invention (docs/90): a cell of an `ui.icon_grid` with entries is
-- as wide as the grid divided by the columns, and its box is `icon + 8 + line + 8` high, where the icon is at most
-- seventy-two pixels. The picture and the name are placed inside that box, and a cell that does not fit its children
-- would paint them over the next cell.

local caps = sys.caps()
if not caps.display then
  console.print('home: this board has no glass')
  return
end

local view = ui.caps()
ui.title('Home')

-- A step of the face is eleven pixels of line with the bold face of the disk and seven with the table of the flash, so
-- the sizes of the page are asked in pixels and the face answers with the steps it has (docs/30).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

local kName = scale_for(22)
local kNameLine = kName * ui.line_height(1)

-- **The box of a cell, in the arithmetic of `icon_grid_cells`** (docs/90): three columns make a cell wide enough for a
-- name, and the icon is the seventy-two pixels the grid gives a mark when the cell is not narrow.
local kColumns = 3
local kIcon = 72
local kPad = 4

-- **A row is the box of a cell** and the same number `icon_grid_cells` computes from the icon and the line (docs/90): the
-- launcher needs it to know how many rows a page holds, because the page is the launcher's and it is the launcher that
-- decides which names reach the grid.
local kCellH = kIcon + 8 + kNameLine + 8

local kMargin = 20
local kTop = 8

-- **The band of the pages takes the foot of the page** (0.154.1): the grid cannot paint a paginator under itself, so the
-- room is kept before the grid is made and the grid is given what is left. The height is a target of a finger and not a
-- line of words, and the arrows are at the two corners a hand expects.
local kPaginator = 56
local kGap = 10   -- the air between the last row of cells and the band
local kArrow = 72 -- how wide an arrow is

-- **The air a grid keeps at its own foot** (`icon_grid_cells`, docs/90): twenty-eight pixels of a grid taller than a
-- hundred and sixty, for the page the control would say if it were the one paging. This launcher paints its own pages, so
-- the number is taken out of the room a page can use —never given to cells— and the arithmetic below is true either way,
-- because twenty-eight is less than a row.
local kFoot = 28

-- **The names of the door, the manifests that have arrived, and the queue between the two** (0.154.0). `known` is filled
-- as the queue runs, and a page laid out again uses it instead of asking the disk a second time.
local ids = {}
local known = {}
local cells = {}      -- the id of an app to its three controls, so a manifest can land in a cell that is already drawn
local queue = {}
local head = 1
local again = false   -- something left the page, so it is laid out again
local grid = nil
local band = {}       -- the three controls of the paginator, so the next page can give them back
local page = 1        -- **the page on the glass** (0.154.1), from one
local pages = 1
local per_page = 0    -- how many cells a page of this glass holds, from the box of the grid
local want_page = nil -- the page a person asked for, taken by the loop and not inside the click of an arrow

-- ------------------------------------------------------------------ the page

local function give_back()
  if grid ~= nil then
    grid:release()
    grid = nil
  end
  for i = 1, #band do
    band[i]:release()
  end
  band = {}
  cells = {}
end

-- **The picture of an app that has none yet**: the box of an icon with the first letter of its name inside it
-- (0.154.0). It is drawn by this file and not shipped as a file because it says what the page knows at that moment —the
-- name of the app— and it is what a cell shows while its manifest has not arrived or its picture cannot be read.
local function placeholder(picture, id)
  picture:clear(15)
  picture:frame(1, 1, kIcon - 2, kIcon - 2, 0)

  local letter = string.upper(string.sub(id, 1, 1))
  if letter == '' then return end

  local scale = 4
  local wide = ui.text_width(letter, scale)
  local high = ui.line_height(scale)
  picture:text((kIcon - wide) // 2, (kIcon - high) // 2, letter, scale, 0)
end

-- **The picture of an app into its cell**: the file the manifest names, over the letter the cell already has, so a
-- picture that cannot be read leaves the letter and not a hole, and the console says why once (docs/30).
local function draw_picture(id, picture, info)
  if info.icon == '' then
    console.print('home: ' .. id .. ' has no picture')
    return
  end

  local drawn, why = picture:image(0, 0, info.icon, kIcon, kIcon)
  if not drawn then
    console.print('home: ' .. id .. ' has no picture: ' .. tostring(why and why.message or why))
  end
end

-- One cell of the page: the picture, the name and the press. `known[id]` is the manifest when it has arrived and `nil`
-- while the queue has not asked for it, and the same code draws both, so a cell that gains a name is not a second page.
local function add_cell(cell_w, cell_h, name_w, id)
  local info = known[id]
  local cell = ui.grid_entry(cell_w, cell_h)
  if cell == nil then return end

  cell.background = 15

  local picture = ui.canvas(kIcon, kIcon)
  if picture ~= nil then
    picture:place((cell_w - kIcon) // 2, 4)
    placeholder(picture, id)
    if info ~= nil then draw_picture(id, picture, info) end
    cell:add(picture)
  end

  local name = ui.label(name_w, kNameLine)
  if name ~= nil then
    name.text = (info ~= nil) and info.title or id
    name.scale = kName
    name.align = 'center'
    name.color = 0
    name.background = 15
    name:place(kPad, kIcon + 6)
    cell:add(name)
  end

  -- The finger and the centre of the dial both come here: the click of a cell is the click of a control (docs/30).
  cell.on_click = function()
    local ok, err = app.run(id)
    if ok then
      console.print('home: ' .. id)
    else
      console.print('home: ' .. id .. ' cannot start: ' .. tostring(err and err.message or err))
    end
  end

  grid:add(cell)
  cells[id] = { cell = cell, picture = picture, name = name }
end

-- **The band of the pages** (0.154.1): an arrow at each side that turns the page and, between them, the page it is and
-- how many there are. An arrow with nowhere to go is `disabled` —drawn, and answered by neither the dial nor a finger—,
-- which is the system's own way of saying «now not this», so a launcher of one page still keeps the band and says so.
--
-- **A press is a wish here too** (docs/30): the click says which page was asked for and the loop lays it out, because an
-- arrow that gave its own control back inside its own callback would be a page drawing into a slot that is already gone.
local function build_paginator(band_y, width)
  local function arrow(x, mark, wanted)
    local button = ui.button(kArrow, kPaginator)
    if button == nil then return end

    button.background = 15
    button:place(x, band_y)

    local word = ui.label(kArrow - 8, kNameLine)
    if word ~= nil then
      word.text = mark
      word.scale = kName
      word.align = 'center'
      word.color = 0
      word.background = 15
      word:place((kArrow - ui.text_width(mark, kName)) // 2, (kPaginator - kNameLine) // 2)
      button:add(word)
    end

    button.disabled = (wanted < 1 or wanted > pages)
    button.on_click = function()
      want_page = wanted
    end
    band[#band + 1] = button
  end

  arrow(kMargin, '<', page - 1)

  local middle = width - 2 * (kArrow + kGap)
  if middle > 40 then
    local number = ui.label(middle, kNameLine)
    if number ~= nil then
      number.text = string.format('%d / %d', page, pages)
      number.scale = kName
      number.align = 'center'
      number.color = 0
      number.background = 15
      -- **The place of the number is the place of the band**: a child of the page counts from the corner of the page and
      -- not from the band, and a number a few pixels under the top of the grid is a number nobody sees (found on the
      -- bench, 8/11: the two arrows came out and the page it is did not).
      number:place(kMargin + kArrow + kGap, band_y + (kPaginator - kNameLine) // 2)
      band[#band + 1] = number
    end
  end

  arrow(width + kMargin - kArrow, '>', page + 1)
end

local function build()
  give_back()

  local width = view.width - 2 * kMargin
  local band_y = view.height - kMargin - kPaginator
  local top = kTop
  local height = band_y - kGap - top
  if width < 200 or height < kCellH then
    console.print('home: the glass is too small for a grid in it')
    return
  end

  -- **The rows of a page, once, from the grid's own arithmetic** (0.154.1, docs/90): a row is the box of a cell and the
  -- foot of a grid taller than a hundred and sixty pixels keeps `kFoot` for the page the control would say. Because the
  -- page is this launcher's, the count of them is the launcher's too — and it is what says how many names reach the grid.
  local rows = (height - ((height > 160) and kFoot or 0)) // kCellH
  if rows < 1 then rows = 1 end
  per_page = kColumns * rows
  pages = (#ids + per_page - 1) // per_page
  if pages < 1 then pages = 1 end
  if page > pages then page = pages end
  if page < 1 then page = 1 end

  -- **The page is the app's whole and not the box of its controls** (0.153.1, docs/40): a control paints inside its own
  -- frame, so the air around the grid keeps whatever the glass had —the picture of the boot— if this page does not wash
  -- it. The wash is also what makes the first present a whole page and not a box of it (docs/50).
  ui.clear(15)

  grid = ui.icon_grid(width, height)
  if grid == nil then
    console.print('home: there is no room for the grid of the apps')
    return
  end

  grid.columns = kColumns
  grid.scale = kName
  grid:place(kMargin, top)

  local cell_w = width // kColumns
  local name_w = cell_w - 2 * kPad

  -- **The cells of this page and no others** (0.154.1): the first one of the page is `(page - 1) * per_page + 1` of the
  -- names, and the rest wait for their page. A grid that was handed every name would keep the cells it cannot fit out of
  -- sight, and with the ring on the cells themselves the page would never follow it.
  local first = (page - 1) * per_page + 1
  local last = first + per_page - 1
  if last > #ids then last = #ids end
  for i = first, last do
    add_cell(cell_w, kCellH, name_w, ids[i])
  end

  build_paginator(band_y, width)

  -- **The ring is on the first cell of the page** (0.153.0, docs/30): a page that left the dial outside would answer
  -- «nothing» to the first press of the middle key, which is the one a person makes. **Of the first cell of the page on
  -- the glass** (0.154.1): after a page turns, the cell a person came for is the first of it, and the arrows stay a walk
  -- to the right of the last cell, which is the way a finger and a key both go.
  --
  -- **The cells are the stops and the grid is not** (0.156.1): a grid that holds the app's cells is a box, and the walk
  -- of the dial goes from the last cell of the page to the first one round the controls that are on the glass. It used
  -- to stop on the grid itself, which paints no ring, and a person read that as the focus leaving the page.
  if ids[first] ~= nil and cells[ids[first]] ~= nil then
    ui.focus(cells[ids[first]].cell)
  end

  -- **The page says which one it is**, and it says it here because this is the one place that lays a page out: a resize,
  -- a manifest that took an app off the page and an arrow all end on this line (0.154.1).
  console.print(string.format('home: page %d / %d (%d cells)', page, pages, math.max(last - first + 1, 0)))

  if #ids == 0 then
    -- **An empty grid says why it is empty** instead of showing nothing: a disk with no app and a page that failed look
    -- the same on the glass, and only one of them is a fault (docs/45).
    local empty = ui.label(width, kNameLine * 2)
    if empty ~= nil then
      empty.text = 'No app on this disk runs on this board'
      empty.scale = kName
      empty.align = 'center'
      empty.color = 0
      empty.background = 15
      empty:place(0, 0)
      grid:add(empty)
    end
  end

  ui.present('gc16')
end

-- ------------------------------------------------------------------ the queue

-- **One app of the queue per turn** (0.154.0): the manifest of the next name, and the cell it belongs to learns the name
-- a person reads and the picture to press. An app that turns out to be a launcher of its own, or one of another board,
-- leaves the page and the page is laid out again without it when the queue is over; one whose manifest cannot be read
-- keeps its name and its letter and says why, which is the only thing this page can honestly do.
local function step()
  local id = queue[head]
  head = head + 1
  if id == nil then return end

  local info = apps.info(id)
  if info == nil then
    console.print('home: ' .. id .. ' has no manifest this system reads')
    return
  end

  if info.launcher or not info.works_here then
    if info.launcher then
      console.print('home: ' .. id .. ' is a launcher')
    else
      console.print('home: ' .. id .. ' needs another board')
    end
    again = true
    for i = #ids, 1, -1 do
      if ids[i] == id then table.remove(ids, i) end
    end
    return
  end

  known[id] = info
  console.print(string.format('home: app %s %s', id, info.title))
  local done = cells[id]
  if done ~= nil then
    if done.name ~= nil then done.name.text = info.title end
    if done.picture ~= nil then draw_picture(id, done.picture, info) end
  end
end

-- ------------------------------------------------------------------ the app

-- **The names, and nothing read yet**: this is what makes the page arrive at once. This app is not in it —a launcher
-- does not list itself— and the rest is asked for one at a time from the loop below.
do
  local all = apps.list()
  if all == nil then
    console.print('home: the door of the apps did not answer')
  else
    for i = 1, #all do
      if all[i] ~= app.id then
        ids[#ids + 1] = all[i]
      end
    end
    -- **The page is in the order of the names of the door and it does not move after** (0.154.0): the titles arrive one at
    -- a time, and a grid that reorders itself when one lands is a grid whose cells jump under the finger. The order of
    -- the disk is not one a person can learn either, so the names are sorted once, here, before the first cell.
    table.sort(ids)
    for i = 1, #ids do queue[i] = ids[i] end
  end
end

console.print(string.format('home: %d ids: %s', #ids, table.concat(ids, ' ')))

build()

-- **The system gave the app another view** (0.153.0, docs/30): the whole glass when the person asks for it from the bar
-- and the glass under the bar when three taps give it back. The page is built again in the new box, with the manifests
-- that have already arrived, and the queue goes on where it was.
function on_resize(width, height)
  view = { width = width, height = height }
  build()
end

-- **The loop is the queue and the paint**: a turn asks for the manifest of one app and shows what changed; a turn with
-- nothing to ask only keeps the kernel turning, and a present with nothing new draws nothing (docs/30, docs/90).
while true do
  local painted = false
  if head <= #queue then
    step()
    painted = true
  elseif again then
    -- **The page is laid out again once**, and only when an app left it: a grid whose cells are laid out for a name that
    -- is not an app of this page any more is a grid with a hole in it (0.154.0).
    again = false
    build()
    painted = true
  elseif want_page ~= nil then
    -- **A page turn is a page laid out again, and it comes from here and not from the arrow** (0.154.1): the press is a
    -- wish, like `app.run`, because the control that took it cannot give itself back while it is running.
    local target = want_page
    want_page = nil
    if target >= 1 and target <= pages and target ~= page then
      page = target
      build()
      painted = true
    end
  end

  if painted then ui.present() end
  sys.delay(50):await()
end
