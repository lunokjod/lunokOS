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

-- **The names of the door, the manifests that have arrived, and the queue between the two** (0.154.0). `known` is filled
-- as the queue runs, and a page laid out again uses it instead of asking the disk a second time.
local ids = {}
local known = {}
local cells = {}      -- the id of an app to its three controls, so a manifest can land in a cell that is already drawn
local queue = {}
local head = 1
local again = false   -- something left the page, so it is laid out again
local grid = nil

-- ------------------------------------------------------------------ the page

local function give_back()
  if grid ~= nil then
    grid:release()
    grid = nil
  end
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
local function add_cell(cell_w, cell_h, name_w, id, index)
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

  -- **The page follows the dial** (0.157.0): the grid is the one that pages —its footer says `N / M` and the cells of the
  -- other pages are out of its frame— and the page it is on is the page of the cell its `index` says. So a cell that
  -- takes the ring tells the grid where it is and asks it to lay itself out again, and the control does the rest: the
  -- page turns with the dial and this file has no paginator of its own (docs/30, docs/90).
  cell.on_focus = function(self, on)
    if not on then return end
    grid.index = index
    grid:invalidate()
  end

  grid:add(cell)
  cells[id] = { cell = cell, picture = picture, name = name }
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
  kIcon = (width // kColumns) - 20
  if kIcon > 72 then kIcon = 72 end
  if kIcon < 24 then kIcon = 24 end
  kCellH = kIcon + 8 + kNameLine + 8

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

-- **The size of the page that is on the glass** (0.168.3): a call of `on_resize` that says the size the page already has is
-- not another view, and building again for it would throw away the canvases of the cells that are already on the glass —
-- seven pictures of 72 x 72 at four bits are 18 144 B and a whole-page paint, for nothing—. **What this guard does not fix,
-- and it is written here so nobody believes it does**: the lost pictures of a launcher. Those were measured in a run where
-- the app was started **twice** —`lunokOS` boots its home app and the bench asked for it again with `--run home_eink`— and
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
