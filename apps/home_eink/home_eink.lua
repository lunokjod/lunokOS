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
-- **The pages are the grid's, and its own footer is the paginator** (0.157.0): this file hands over **every** cell —all
-- the apps of the disk— and the grid keeps the page of the cell the dial is on and paints `N / M` at its foot when there
-- is more than one. That footer, and the cells of the other pages kept out of sight, are `icon_grid_cells`, the same
-- arithmetic the paint and the finger use (docs/90); what this file does is tell the control **which cell the dial is
-- on**, in the `on_focus` of each cell, because that number is the page. **A paginator of its own was a control the
-- system already has**: two arrows and a number under the grid are gone (the owner, 8/11: *«usa sempre els UIControls
-- que hi hagin abans de fer-ne un de nou»*).
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
--
-- **AND THE SECOND TIME THIS PAGE COMES UP IT COSTS NO DISK** (0.179.0): the manifests and the pictures of the cells are
-- kept in a **memory of the session** that the kernel holds for this app under the key `apps`, so a launcher that is left
-- and started again —which on this board is every time an app ends— draws its real names and its real pictures in the
-- first turn, and a dial that walks over a cell paints a copy of a picture it already decoded instead of reading the file
-- and decoding it again. The block is asked once (`sys.session`), it is zeroed when it is new —which is how this file tells
-- a cold cache from a warm one— and it lives until the board is switched off **or the memory runs short**: it is the first
-- thing the system gives back when a block does not fit, because it is the only thing it can make again (docs/30, docs/75).

local caps = sys.caps()
if not caps.display then
  console.print('home: this board has no glass')
  return
end

local view = ui.caps()

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

local kName = scale_for(33)
local kNameLine = kName * ui.line_height(1)

-- **The box of a cell, in the arithmetic of `icon_grid_cells`** (docs/90): three columns make a cell wide enough for a
-- name, and the icon is the seventy-two pixels the grid gives a mark when the cell is not narrow.
local kColumns = 2
-- **The box of a picture is what the kernel computes for the cell** (0.166.1): a grid makes a mark as wide as its cell
-- minus the padding, and the app has to ask for exactly that or the picture sits small inside a big box. It is worked out
-- in `build()` because it depends on the width of the view.
local kIcon = 72
local kPad = 4

-- **A row is the box of a cell** and the same number `icon_grid_cells` computes from the icon and the line (docs/90): the
-- cells are the app's and their box is what the picture and the name are placed in.
local kCellH = kIcon + 8 + kNameLine + 8

local kMargin = 20
local kTop = 8

-- ------------------------------------------------------------------ the cache of the session

-- **What is kept, and where**: a header of eight bytes —the magic and the side of a picture, so a cache of another size of
-- cell is not painted on this one—, the records of the apps and then their pictures, four bits a pixel and in the order of
-- the block (`cv:capture` and `cv:blit` are a copy and not a decode). A record is `id`, the title, the icon and the number
-- of the picture, and the number is how this file knows whether a picture is in the block or has to be read from the disk.
-- **The size asked for, and what it falls back to when the board cannot give it** (0.179.1): a block that holds the records
-- and no picture is still worth having —the manifests are what the queue pays for one per turn— and a block that holds
-- neither is not the end of the page. The pieces come down and the last one is the records alone —1 536 B, plus the eight of
-- the header— which is what a board with a crowd of apps or with the memory spent gets (docs/30, docs/75).
local kCacheSteps = { 32768, 24576, 16384, 8192, 1544 }
local kCacheMagic = 'HEC1'
local kCacheText = 8
local kCacheTextBytes = 1528
local kCachePictures = kCacheText + kCacheTextBytes

local cache = nil          -- the block, or nil when the system gave none
local cached = {}          -- the id of an app to { title=, icon=, slot= }, as the block says
local slots = 0            -- how many pictures the block holds
local warm = {}            -- the ids whose picture is in the block, so a paint is a copy

-- **The names of the door, the manifests that have arrived, and the queue between the two** (0.154.0). `known` is filled
-- as the queue runs, and a page laid out again uses it instead of asking the disk a second time.
local ids = {}
local known = {}
local cells = {}      -- the id of an app to its three controls, so a manifest can land in a cell that is already drawn
local queue = {}
local head = 1
local again = false   -- something left the page, so it is laid out again
local grid = nil

-- **What a picture takes in the block**, in bytes: four bits a pixel and the stride of the canvas the kernel makes.
local function picture_bytes()
  return ((kIcon + 1) // 2) * kIcon
end

-- **The block is asked once and read**: a cold one has no magic —the kernel gives it zeroed— and every record of it is
-- taken back into `cached`. A block whose side is not the side of this page is not read at all: the pictures in it were
-- measured for another cell. A record says whether its picture is really in the block, because an app whose picture failed
-- to be drawn has a number and no picture, and a zeroed box painted as an icon is a black square (docs/30).
local function load_cache()
  cache = nil
  cached = {}
  slots = 0
  warm = {}
  local block, err = nil, nil
  for i = 1, #kCacheSteps do
    block, err = sys.session('apps', kCacheSteps[i])
    if block ~= nil then break end
  end
  if block == nil then
    console.print('home: no cache of the session: ' .. tostring(err and err.message or err))
    return
  end
  cache = block
  if cache:read(0, 4) ~= kCacheMagic then
    console.print(string.format('home: cache cold, %d bytes', cache:size()))
    return
  end
  if cache:read(4, 1):byte(1) ~= kIcon then
    console.print('home: cache of another size of cell, building it again')
    return
  end
  -- **The text ends at the first zero** (0.179.0): the block is read whole —the records are shorter than their window— and a
  -- zero is not in any of them, so what is after the last one is the kernel's zeroing and not a record with a broken tail
  -- (a launcher that lost the last app of its cache asked for it again at every start, which is how this was found).
  local text = cache:read(kCacheText, kCacheTextBytes)
  local zero = text:find('\0', 1, true)
  if zero ~= nil then text = text:sub(1, zero - 1) end
  local count = 0
  local drawn = 0
  for line in text:gmatch('[^\n]+') do
    local id, title, icon, slot, has = line:match('^([^\t]+)\t([^\t]*)\t([^\t]*)\t(%d+)\t([01])$')
    if id ~= nil then
      slot = tonumber(slot)
      cached[id] = { title = title, icon = icon, slot = slot }
      if slot + 1 > slots then slots = slot + 1 end
      if has == '1' then
        warm[id] = true
        drawn = drawn + 1
      end
      count = count + 1
    end
  end
  console.print(string.format('home: cache warm, %d apps, %d pictures', count, drawn))
end

-- **And it is written when the page is whole**: the magic goes last, so a run that ends in the middle of the queue does not
-- leave a header that promises records the block does not have. A title or a path with the two characters of the format in
-- it is not written —a cache that breaks the parse is a cache that is never read again— and the app is asked next time.
local function save_cache()
  if cache == nil then return end
  local text = {}
  local slot = 0
  local held = 0
  for i = 1, #ids do
    local id = ids[i]
    local info = known[id]
    if info ~= nil then
      local title = info.title
      local icon = info.icon
      if not title:find('[\t\n]') and not icon:find('[\t\n]') and not id:find('[\t\n]') then
        local was = cached[id]
        if was ~= nil then
          slot = was.slot
        else
          slot = slots
          slots = slots + 1
        end
        cached[id] = { title = title, icon = icon, slot = slot }
        local has = warm[id] and '1' or '0'
        if has == '1' then held = held + 1 end
        text[#text + 1] = string.format('%s\t%s\t%s\t%d\t%s', id, title, icon, slot, has)
      end
    end
  end
  local body = table.concat(text, '\n')
  if #body > kCacheTextBytes then
    console.print('home: too many apps for the cache of the session')
    return
  end
  cache:write(kCacheText, body)
  cache:write(4, string.char(kIcon))
  cache:write(0, kCacheMagic)
  console.print(string.format('home: cache kept, %d apps, %d pictures', #text, held))
end

-- ------------------------------------------------------------------ the page

local function give_back()
  if grid ~= nil then
    grid:release()
    grid = nil
  end
  cells = {}
end

-- **A picture that could not be drawn is said once** (0.179.0, and it was said at the arrival of the manifest before):
-- the paint is the one that knows —it is the one that reads the file— and a page that says it at every turn says it every
-- time the dial walks over the cell (docs/30, docs/90).
local said = {}
local function say_the_fault(id, why)
  if said[id] then return end
  said[id] = true
  if why == nil then
    console.print('home: ' .. id .. ' has no picture')
  else
    console.print('home: ' .. id .. ' has no picture: ' .. tostring(why and why.message or why))
  end
end

-- **The picture of a cell, painted again every time the cell asks for it** (0.178.0): the letter of the app on paper and,
-- over it, the file the manifest names. It is one function because the cell can be **on ink** —the dial is on it— and then
-- the whole picture is turned over at the end: the paper of the icon becomes the black of the cell and the greys of the
-- drawing stay, which is what the old launcher did with one call over its selected cell and the reason its icons looked
-- as if they had no box (docs/30, docs/92).
local function paint_picture(id, picture, info, inverted)
  -- **A picture that is already in the block is a copy and not a decode** (0.179.0): the same pixels the file and the
  -- decoder made the first time, and no disk at all. It is always the paper face —the ink is the `invert` at the end— so
  -- the cache holds one picture per app and not two (docs/30, docs/75).
  local record = cached[id]
  if cache ~= nil and record ~= nil and warm[id] and cache:size() >= kCachePictures + (record.slot + 1) * picture_bytes() then
    local at = kCachePictures + record.slot * picture_bytes()
    if picture:blit(cache, at, 0, 0, kIcon, kIcon) then
      if inverted then picture:invert(0, 0, kIcon, kIcon) end
      return
    end
    warm[id] = nil
  end

  picture:clear(15)

  local letter = string.upper(string.sub(id, 1, 1))
  if letter ~= '' then
    local scale = 4
    local wide = ui.text_width(letter, scale)
    local high = ui.line_height(scale)
    picture:text((kIcon - wide) // 2, (kIcon - high) // 2, letter, scale, 0)
  end

  if info ~= nil then
    if info.icon == '' then
      say_the_fault(id, nil)
    else
      local drawn, why = picture:image(0, 0, info.icon, kIcon, kIcon)
      if not drawn then say_the_fault(id, why) end
    end
  end

  -- **And what was just drawn is kept**: the paper face only, because the ink is the invert of the same pixels, and only
  -- when the block has a place for it —an app whose manifest has not arrived has no record yet, and a block of a board with a
  -- crowd of apps may hold the records and no picture at all— (docs/30, docs/75).
  if cache ~= nil and record ~= nil and not inverted and info ~= nil and cache:size() >= kCachePictures + (record.slot + 1) * picture_bytes() then
    local at = kCachePictures + record.slot * picture_bytes()
    if picture:capture(cache, at) ~= nil then warm[id] = true end
  end

  if inverted then picture:invert(0, 0, kIcon, kIcon) end
end


-- One cell of the page: the picture, the name and the press. `known[id]` is the manifest when it has arrived and `nil`
-- while the queue has not asked for it, and the same code draws both, so a cell that gains a name is not a second page.
local function add_cell(cell_w, cell_h, name_w, id, index)
  local info = known[id]
  local cell = ui.grid_entry(cell_w, cell_h)
  if cell == nil then return end

  cell.background = 15

  -- **A picture with no room says so** (0.174.5): the cell shows the letter and the console says why, because a grid
  -- of names with four icons missing and no line about it is a page that hides what the memory answered (`ui.canvas`
  -- gives `nil, error` and not a box that is not there, docs/30).
  local picture, no_room = ui.canvas(kIcon, kIcon)
  if picture ~= nil then
    picture:place((cell_w - kIcon) // 2, 4)
    -- **The picture paints itself, and it hears whether it is on ink** (0.178.0): the cell asks its children to draw again
    -- when it turns over, and the second argument is the face it leaves behind. A canvas of the app with no `on_draw`
    -- cannot turn over —it would compose the same white box on the block— and that is all this callback is for
    -- (docs/30, docs/92).
    picture.on_draw = function(self, inverted)
      paint_picture(id, self, known[id], inverted)
    end
    -- **And nothing is painted here**: a canvas that was just made owes a draw, so the system asks it —with `false`, the
    -- paper face— and a second paint of the same pixels would be the page paying twice for one picture (docs/30). It is
    -- also the moment the picture is kept in the cache of the session, which is why this file does not draw it here.
    cell:add(picture)
  else
    console.print('home: ' .. id .. ' has no picture: ' .. tostring(no_room and no_room.message or no_room))
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

  -- **The page follows the dial** (0.157.0): the grid is the one that pages —its footer says `N / M` and the cells of the
  -- other pages are out of its frame— and the page it is on is the page of the cell its `index` says. So a cell that
  -- takes the ring tells the grid where it is, and the control does the rest: the page turns with the dial and this file
  -- has no paginator of its own (docs/30, docs/90).
  -- **And asking the grid to repaint itself here is not needed** (0.177.3): the field of `index` asks the kernel what the
  -- move costs, lays the cells out again when the page turns and paints nothing at all when it does not —a ring that
  -- walks inside a page is two cells of the app and no paper of the grid—, so an invalidate of the whole grid here was
  -- nine pictures painted for one ring (docs/30, docs/90).
  cell.on_focus = function(self, on)
    if not on then return end
    grid.index = index
  end

  grid:add(cell)
  cells[id] = { cell = cell, picture = picture, name = name }
end

-- **The size of the cell, which is the key of the cache** (0.179.0): the block keeps pictures of a side, and a page that
-- has been resized has a different one —so the side is measured once, before the cache is read, and a block of another side
-- is not painted on this page—. It is the arithmetic `icon_grid_cells` does and not this file's (docs/30, docs/90).
local function measure()
  local width = view.width - 2 * kMargin
  kIcon = (width // kColumns) - 20
  if kIcon > 72 then kIcon = 72 end
  if kIcon < 24 then kIcon = 24 end
  kCellH = kIcon + 8 + kNameLine + 8
end

local function build()
  give_back()

  local width = view.width - 2 * kMargin
  local top = kTop
  local height = view.height - top - kMargin
  if width < 200 or height < 120 then
    console.print('home: the glass is too small for a grid in it')
    return
  end

  -- **The mark of a cell is as wide as the cell allows and no more than the system draws** (0.166.1): the same
  -- arithmetic `icon_grid_cells` does, with the padding of ten and the ceiling of a hundred and twenty, so the picture
  -- the app makes is the box the grid has for it (docs/30, docs/90).
  measure()

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
  -- **The cell the dial is on, which is also the page the grid is on**: the first one until somebody walks (0.157.0).
  grid.index = 1
  grid:place(kMargin, top)

  local cell_w = width // kColumns
  local name_w = cell_w - 2 * kPad

  -- **Every name is a cell of the grid and the pages are the control's** (0.157.0): the cells that do not fit its frame
  -- are placed out of sight by the grid and come back when the dial reaches them, and its foot says the page it is
  -- (`icon_grid_cells`, docs/90). A launcher that handed it one page at a time was a launcher that owned a paginator.
  for i = 1, #ids do
    add_cell(cell_w, kCellH, name_w, ids[i], i)
  end

  -- **The ring is on the first cell** (0.153.0, docs/30): a page that left the dial outside would answer «nothing» to the
  -- first press of the middle key, which is the one a person makes. **The cells are the stops and the grid is not**
  -- (0.156.1): a grid that holds the app's cells is a box, and the walk of the dial goes from the last cell to the first
  -- one round the controls that are on the glass.
  if ids[1] ~= nil and cells[ids[1]] ~= nil then
    ui.focus(cells[ids[1]].cell)
  end

  -- **What this page can say of itself**: the cells it holds. The page and how many there are is the grid's to say, at
  -- its foot, and the console says how many names the door answered (0.157.0).
  console.print(string.format('home: %d cells', #ids))

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
  -- **The record of this app in the block, with a place of its own for its picture**: the paint that follows is the one
  -- that reads the file and keeps the pixels, and it needs the number before it draws (0.179.0, docs/75).
  if cached[id] == nil then
    cached[id] = { title = info.title, icon = info.icon, slot = slots }
    slots = slots + 1
  end
  console.print(string.format('home: app %s %s', id, info.title))
  local done = cells[id]
  if done ~= nil then
    if done.name ~= nil then done.name.text = info.title end
    if done.picture ~= nil then
      -- **And the picture paints again, by itself**: its `on_draw` is what knows how to put the file over the letter and
      -- how to turn the whole box over when the dial is on the cell (0.178.0, docs/30).
      done.picture:invalidate()
    end
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
  end
end

console.print(string.format('home: %d ids: %s', #ids, table.concat(ids, ' ')))

-- **The cache of the session, before the first cell** (0.179.0): the side of a picture is what names its place in the block,
-- so the cell is measured first, and an app the block already knows needs neither its manifest nor its file for this page
-- to be whole —the queue is only the apps that are not in it— (docs/30, docs/75).
measure()
load_cache()
for i = 1, #ids do
  local was = cached[ids[i]]
  if was ~= nil then
    known[ids[i]] = { title = was.title, icon = was.icon }
  else
    queue[#queue + 1] = ids[i]
  end
end

-- **The size of the page that is on the glass** (0.168.3): a call of `on_resize` that says the size the page already has is
-- not another view, and building again for it would throw away the canvases of the cells that are already on the glass —
-- seven pictures of 72 x 72 at four bits are 18 144 B and a whole-page paint, for nothing—. **What this guard does not fix,
-- and it is written here so nobody believes it does**: the lost pictures of a launcher. Those were measured in a run where
-- the app was started **twice** —`lunokOS` boots its home app and the emulator asked for it again with `--run home_eink`— and
-- the log of it says `[I][APPS] home_eink` twice; started once, the page is built once (measured, 8/11) (docs/30, docs/50).
local page_w = 0
local page_h = 0

build()
page_w = view.width
page_h = view.height

-- **The system gave the app another view** (0.153.0, docs/30): the whole glass when the person asks for it from the bar
-- and the glass under the bar when three taps give it back. The page is built again in the new box, with the manifests
-- that have already arrived, and the queue goes on where it was. **And a call that says the size the page already has is
-- not another view**: rebuilding for it throws away the canvases of the cells that are already on the glass (0.168.3).
function on_resize(width, height)
  if width == page_w and height == page_h then return end
  page_w = width
  page_h = height
  view = { width = width, height = height }
  -- **A page of another size has cells of another size**, and the pictures in the block were measured for the old one: they
  -- are not painted on this page, and the ones this run draws are kept in their places (the magic is rewritten with the new
  -- side, so a block of two sides is a block of none and is built again at the next start) — docs/30, docs/75.
  local was = kIcon
  measure()
  if kIcon ~= was then
    warm = {}
    cached = {}
    slots = 0
    if cache ~= nil then cache:write(0, '    ') end
  end
  build()
end

-- **The loop is the queue and the paint**: a turn asks for the manifest of one app and shows what changed; a turn with
-- nothing to ask only keeps the kernel turning, and a present with nothing new draws nothing (docs/30, docs/90).
local kept = false

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
  elseif head > #queue and not kept then
    -- **And the cache is written once the page is whole** (0.179.0): every manifest has arrived and every picture it could
    -- draw is in the block, so the next run of this launcher —which is every time an app ends— has no disk to read
    -- (docs/30, docs/75).
    kept = true
    save_cache()
  end

  -- **The page goes up as the cells come** (0.166.1): one present per cell, right after its picture, because a single
  -- present of the whole page when the queue was over was tried and measured wrong —it lands before the cells arrive, so
  -- the last pictures of the grid stay unwritten— and what the owner asked for is that the cell the system has just
  -- filled is the one that goes to the glass (docs/30, docs/50).
  if painted then
    ui.present('gc16')
  end
  sys.delay(50):await()
end
