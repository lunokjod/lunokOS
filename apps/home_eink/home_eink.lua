-- home_eink: **the launcher of the glass** (0.153.0).
--
-- The apps of the disk, in a grid of pictures and names: a finger on a picture starts the app it belongs to. The list is
-- not read from `/apps` by this file —**the system reads it**—, because the door of the apps already puts the flash and
-- the card together, refuses a folder that is not an app and reads its manifest (docs/40). `apps.list()` hands the four
-- things this page needs of each app: the id, the name, the picture and whether it is a launcher of its own.
--
-- **A launcher does not list itself, and no launcher lists another.** An app that is a launcher says `"launcher": true`
-- in its manifest and every launcher skips it; this one is not the exception, and the line `not a.launcher` below is
-- what makes this app invisible in its own grid and in any other (docs/30, docs/40). It is not a hidden app: `apps` lists
-- it, `run home_eink` starts it, and the board can come up inside it (`cfg home home_eink`).
--
-- **A press is a wish and not a switch**: `app.run(id)` answers at once and the kernel does the change after the turn,
-- because the app that asks is the one the interpreter belongs to (docs/30, docs/90). The second argument is the way
-- back: with it the person returns to this page when the app they opened ends, which is the only way back on a board
-- whose single way out of an app is a key. That is why this file passes it.
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

local kMargin = 20
local kTop = 8

-- What the door answered, without the launchers and without the apps of another board.
local entries = {}
local left_out = 0
local grid = nil

-- ------------------------------------------------------------------ the page

local function give_back()
  if grid ~= nil then
    grid:release()
    grid = nil
  end
end

-- **The apps of the door, once per build**: the grid is what the page is, so the list is read when the page is made and
-- not while the loop turns — a disk that is read every fifty milliseconds is a battery spent on nothing (docs/30).
local function collect()
  entries = {}
  left_out = 0

  local all = apps.list()
  if all == nil then
    console.print('home: the door of the apps did not answer')
    return
  end

  for i = 1, #all do
    local app = all[i]
    -- **A launcher of its own is not on a launcher's grid**, and the one that is asking is one of them.
    if not app.launcher then
      if app.works_here then
        entries[#entries + 1] = app
      else
        -- An app that names another board is not pressable here, and a picture that opens nothing is worse than no
        -- picture: it is counted and said in the same line as the rest (docs/30).
        left_out = left_out + 1
      end
    end
  end

  -- **The grid is in the order of the names** (0.153.0): the order of a directory is the order of the disk, and a page
  -- whose cells move between two boots is a page a person cannot learn. The id is the tiebreaker, because two apps may
  -- say the same name and a grid whose order depends on which of the two the disk answered first is not an order.
  table.sort(entries, function(a, b)
    if a.title == b.title then return a.id < b.id end
    return a.title < b.title
  end)
end

local function open(id)
  local ok, err = app.run(id)
  if ok then
    console.print('home: ' .. id)
  else
    console.print('home: ' .. id .. ' cannot start: ' .. tostring(err and err.message or err))
  end
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

  -- **The page is the app's whole and not the box of its controls** (0.153.0, docs/40): a control paints inside its own
  -- frame, so the air around the grid keeps whatever the glass had —the picture of the boot— if this page does not wash
  -- it. The wash is also what makes the first present a whole page and not a box of it (docs/50).
  -- **The page is the app's whole and not the box of its controls** (0.153.0, docs/40): a control paints inside its own
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

  -- **A cell is as wide as the grid divided by the columns**, which is the arithmetic the grid itself paints with: the
  -- control is placed by the system inside its box, and the picture and the name are this file's and have to fit in it.
  local cell_w = width // kColumns
  local cell_h = kIcon + 8 + kNameLine + 8
  local name_w = cell_w - 2 * kPad
  local first_cell = nil

  for i = 1, #entries do
    local app = entries[i]
    local cell = ui.grid_entry(cell_w, cell_h)
    if cell ~= nil then
      cell.background = 15

      local picture = ui.canvas(kIcon, kIcon)
      if picture ~= nil then
        picture:place((cell_w - kIcon) // 2, 4)
        -- **A cell with no picture says so once**, and the name is still there: a launcher with an empty square and no
        -- explanation is a person wondering whether the tap worked. An app whose manifest names no `icon` and an app
        -- whose picture cannot be read are the same square on the glass and they are said the same way (docs/30).
        if app.icon == '' then
          console.print('home: ' .. app.id .. ' has no picture')
        else
          local drawn, why = picture:image(0, 0, app.icon, kIcon, kIcon)
          if not drawn then
            console.print('home: ' .. app.id .. ' has no picture: ' ..
                            tostring(why and why.message or why))
          end
        end
        cell:add(picture)
      end

      local name = ui.label(name_w, kNameLine)
      if name ~= nil then
        name.text = app.title
        name.scale = kName
        name.align = 'center'
        name.color = 0
        name.background = 15
        name:place(kPad, kIcon + 6)
        cell:add(name)
      end

      -- The finger and the centre of the dial both come here: the click of a cell is the click of a control (docs/30).
      cell.on_click = function()
        open(app.id)
      end
      grid:add(cell)
      if first_cell == nil then first_cell = cell end
    end
  end

  -- **The ring is on the first cell of the page** (0.153.0, docs/30): a grid is a stop of the dial of its own —the arrows
  -- fall on it before they walk its cells—, so a launcher that left the dial outside would answer "nothing" to the first
  -- press of the middle key, which is the one a person makes.
  if first_cell ~= nil then
    ui.focus(first_cell)
  end

  if #entries == 0 then
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

-- ------------------------------------------------------------------ the app

collect()

-- What the page is made of, said out loud: the count, what was left out because it needs another board, and the ids in
-- the order of the grid, which is what a test reads to know what a finger is pressing (docs/85).
-- **One line per app of the grid**, in the order the cells are in, and then the two numbers: what a test reads to know
-- which cell a finger is pressing, and what a person reads in the console when they ask what the page is made of
-- (docs/85).
local ids = {}
for i = 1, #entries do
  console.print(string.format('home: app %s %s', entries[i].id, entries[i].title))
  ids[i] = entries[i].id
end
if left_out > 0 then
  console.print(string.format('home: %d apps, %d left out', #entries, left_out))
else
  console.print(string.format('home: %d apps', #entries))
end

build()

-- **The system gave the app another view** (0.153.0, docs/30): the whole glass when the person asks for it from the bar
-- and the glass under the bar when three taps give it back. The grid is built again in the new box and the door is asked
-- again with it, because an app installed while this page was up is an app the next view has to show.
function on_resize(width, height)
  view = { width = width, height = height }
  collect()
  build()
end

-- **The page is a tree of controls and the finger, the dial and the keys are the system's**: what this loop has to do is
-- keep the kernel turning and offer the page again when something of it has changed. A present with nothing new draws
-- nothing (docs/30, docs/90).
while true do
  ui.present()
  sys.delay(50):await()
end
