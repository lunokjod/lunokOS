-- about: who is behind this, and what it is made of — the port of the paper project's About (0.122.0).
--
-- **Two screens, as the original**: a cover with one big line and the list of the pieces, and the detail of
-- one piece, which opens with a finger and comes back with Back.
--
-- **The list is this board's and not a table of libraries.** The original read a `libs.lua` that its build
-- wrote from the working tree; the disk of this system does not carry the source tree, so what can be told
-- here without lying is what the board **is** at this moment: the system, the interpreter, the glass, the
-- finger, the disk, the radio and the cell. Every number comes from a call of docs/30 and none is written in
-- this file.
--
-- **There is no `visible` in this system** (docs/30, docs/90), so a screen is built and given back and not
-- hidden: opening a piece returns the cover to the system and builds the detail, and Back does the other way
-- round. It is the same two screens of the original, said the way this system has them —and `release()`
-- takes the fills of a control with it, so giving back the panel gives back the whole screen—.
--
-- **The sizes are the paper project's, in pixels of line** —`small` 33 and `big` 82—, asked through
-- `ui.line_height` as the calculator does, and a text that does not fit a label ends in "..." here: so the
-- sentences are broken in this file, with the same measurement the label draws with (docs/30, docs/90).

local caps = sys.caps()
if not caps.display then
  console.print('about: this board has no glass')
  return
end

local view = ui.caps()
ui.title('About')

-- A step of the face is eleven pixels of line with the bold face of the disk and seven with the table of the
-- flash, so the sizes of the original are asked in pixels and the face answers with the steps it has (0.121.0).
local function scale_for(line_px)
  local step = ui.line_height(1)
  if step <= 0 then step = 1 end

  local scale = math.floor(line_px / step + 0.5)
  if scale < 1 then scale = 1 end
  if scale > 16 then scale = 16 end

  return scale
end

local kSmall = scale_for(33)
local kBig = scale_for(82)

local kMargin = 20
local kGap = 10
local kRow = 64

-- What a label of this size takes for one line of this scale, plus the air above and below it.
local function line_height(scale)
  return scale * ui.line_height(1) + 6
end

-- The pieces of this board, and what each one says. Filled by `collect()` at start.
local pieces = {}
-- The control of the screen that is on the page, which is given back when the other one is built.
local screen = nil

-- **The lines a text takes in a label of that width**: a label of this system does not wrap —a text that does
-- not fit ends in "..."— so the app breaks it, and it measures with the same call the label draws with
-- (docs/30, docs/90).
local function broken(text, width, scale)
  local lines = {}
  local line = ''

  for word in text:gmatch('%S+') do
    local try = (line == '') and word or (line .. ' ' .. word)

    if line ~= '' and ui.text_width(try, scale) > width then
      lines[#lines + 1] = line
      line = word
    else
      line = try
    end
  end

  if line ~= '' then lines[#lines + 1] = line end

  return lines
end

local function make_label(width, text, scale, align, valign)
  local label = ui.label(width, line_height(scale))
  if label == nil then return nil end

  label.text = text
  label.scale = scale
  label.align = align or 'left'
  label.valign = valign or 'top'
  label.color = 0
  label.background = 15

  return label
end

-- A sentence on as many labels as it needs, all of them added to `parent`.
local function add_lines(parent, width, text, scale, align)
  local made = 0
  local lines = broken(text, width, scale)

  for i = 1, #lines do
    local label = make_label(width, lines[i], scale, align)
    if label ~= nil then
      parent:add(label)
      made = made + 1
    end
  end

  return made
end

-- --------------------------------------------------------------- what this is

local function count_true(table_of_caps)
  local n = 0
  for _, value in pairs(table_of_caps) do
    if value == true then n = n + 1 end
  end

  return n
end

-- The capabilities of the whole system, as the count the `caps` command prints.
local kCapabilities = 19

local function collect()
  pieces = {}

  -- The system itself, and the band it runs on.
  pieces[#pieces + 1] = {
    name = sys.name,
    value = sys.version,
    what = 'The kernel, the page, the apps and the band they run on.',
    numbers = string.format('board %s%s, %d of %d capabilities', sys.board,
      sys.emulator and ' (the bench)' or ' (the board)', count_true(caps), kCapabilities),
  }

  -- The interpreter, and the memory it is allowed.
  local heap = sys.heap()
  local memory = string.format('%d B used of %d, %d B free, %d B at the peak', heap.used, heap.limit, heap.free,
    heap.peak)
  if heap.extra_total > 0 then
    memory = memory ..
      string.format(', and %d B free of the %d B of extra memory this chip can address', heap.extra_free,
        heap.extra_total)
  end
  pieces[#pieces + 1] = {
    name = 'Lua',
    -- `sys.lua` answers "Lua 5.4.7": the name of the piece is already `Lua`, so the row says the version
    -- and not the name twice.
    value = (sys.lua:gsub('^Lua%s+', '')),
    what = 'The interpreter every app runs in, with a ceiling of its own.',
    numbers = memory,
  }

  -- The glass.
  if caps.display then
    pieces[#pieces + 1] = {
      name = 'The glass',
      value = string.format('%d x %d', view.width, view.height),
      what = 'A page: an image, and the region of it that changed.',
      numbers = string.format('%d levels, and the view sits at %d,%d; the bar of the system takes the rest',
        view.levels, view.x, view.y),
    }
  else
    pieces[#pieces + 1] = {
      name = 'The glass',
      value = 'none',
      what = 'This board has no panel.',
      numbers = 'nothing is painted, and the console is the page',
    }
  end

  -- The finger.
  if caps.touch then
    local panel = touch.caps()
    pieces[#pieces + 1] = {
      name = 'The finger',
      value = string.format('%d points', panel.points),
      what = 'The touch panel, and the shapes the system makes out of a finger.',
      numbers = string.format('%d x %d in the frame of the chip', panel.width, panel.height),
    }
  else
    pieces[#pieces + 1] = {
      name = 'The finger',
      value = 'none',
      what = 'This board has no touch panel.',
      numbers = 'the keys and the dial of the board are the way in',
    }
  end

  -- The disk: the volume the system lives in, and how much of it is left.
  local mounts = fs.mounts()
  local room = 'the room of this volume is not known'
  local space = fs.space('/littlefs')
  if space ~= nil then
    room = string.format('%d B free of %d', space.free, space.total)
  end
  pieces[#pieces + 1] = {
    name = 'The disk',
    value = tostring(#mounts) .. ' mounts',
    what = 'What the board keeps: the files, the apps and what they wrote.',
    numbers = '/littlefs has ' .. room,
  }

  -- The radio.
  local radio = net.caps()
  local there = {}
  if radio.wifi then there[#there + 1] = 'wifi' end
  if radio.http then there[#there + 1] = 'http' end
  if radio.tls then there[#there + 1] = 'tls' end
  if radio.prov then there[#there + 1] = 'provisioning' end
  local what_radio = 'The network: the scan, the provisioning and the pages.'
  local saved = 'no network is saved on this board'
  if net.provisioned() then saved = 'the board has a network saved' end
  pieces[#pieces + 1] = {
    name = 'The radio',
    value = (#there == 0) and 'none' or table.concat(there, ', '),
    what = what_radio,
    numbers = saved .. '; it is switched on for a job and off when the queue is empty',
  }

  -- The cell.
  if caps.battery then
    local cell = power.battery()
    if cell ~= nil then
      pieces[#pieces + 1] = {
        name = 'The cell',
        value = string.format('%d %%', cell.pct),
        what = 'The battery of the board, read by its gauge.',
        numbers = string.format('%d mV', cell.mv),
      }
    else
      pieces[#pieces + 1] = {
        name = 'The cell',
        value = 'unknown',
        what = 'The board has a gauge and it does not answer.',
        numbers = 'a board that cannot say is not a board that says zero',
      }
    end
  else
    pieces[#pieces + 1] = {
      name = 'The cell',
      value = 'none',
      what = 'This board has no fuel gauge.',
      numbers = 'the battery is not measured here',
    }
  end
end

-- ----------------------------------------------------------------- the screens

local show -- the detail, which the cover's rows call
local back -- the cover again, which the detail's button calls
local build_cover

-- **The screen that is on the page is given back, fills and all** (docs/30, docs/90): `release()` marks the
-- control and its fills, and the system takes the buffers when the page has been painted over.
local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
end

build_cover = function()
  -- **The page is the app's and the app owns all of it**: a control paints inside its own box, so the air
  -- around the panel would keep whatever the glass had before it —the drawing of the boot— unless the app
  -- washes the page first (docs/30, docs/50).
  ui.clear(15, 'gc16')

  local page = ui.panel(view.width, view.height, 'vertical')
  page.padding = kMargin
  page.gap = kGap
  page:place(0, 0)

  -- One big line: the name of the system and its version, which is what a person wants to read first on a
  -- page called About. The original put its motto here; a motto is not mine to write.
  local motto = make_label(view.width - 2 * kMargin, sys.name .. ' ' .. sys.version, kBig, 'center', 'middle')
  if motto ~= nil then page:add(motto) end

  -- How many there are, and what to do with them.
  add_lines(page, view.width - 2 * kMargin,
    string.format('%d pieces of this board, by @lunokjod. Tap one to see its numbers.', #pieces), kSmall)

  -- One row a piece, which is the original's button list: a frame a finger can press with the name and the
  -- value centred inside it.
  for i = 1, #pieces do
    local row = ui.button(view.width - 2 * kMargin, kRow)
    if row ~= nil then
      row.background = 15
      row.border = 0
      local text = make_label(view.width - 2 * kMargin - 24, pieces[i].name .. '   ' .. pieces[i].value, kSmall,
        'center', 'middle')
      if text ~= nil then
        -- **The label is set in and not on the frame**: the ring of the focus is a pause and a band inside
        -- the border, and a label that covers the border hides the only sign of where the dial is (docs/30).
        text:place(12, 12)
        row:add(text)
      end
      row.on_click = function() show(i) end
      page:add(row)
    end
  end

  screen = page
  ui.present('gc16')
end

-- **One piece, with its name, what it is and its numbers**, and Back under it: the detail of the original.
show = function(index)
  local piece = pieces[index]
  if piece == nil then return end

  give_back()

  -- The page is washed before the detail is built, the same as the cover (docs/30, docs/50).
  ui.clear(15, 'gc16')
  console.print('about: open ' .. piece.name .. ' ' .. piece.value)

  local page = ui.panel(view.width, view.height, 'vertical')
  page.padding = kMargin
  page.gap = kGap
  page:place(0, 0)

  add_lines(page, view.width - 2 * kMargin, piece.name .. '   ' .. piece.value, kSmall)
  add_lines(page, view.width - 2 * kMargin, piece.what, kSmall)
  add_lines(page, view.width - 2 * kMargin, piece.numbers, kSmall)

  -- A cell that takes what is left, which is what pushes the button to the bottom of the page.
  local spring = ui.panel(10, 10, 'vertical')
  if spring ~= nil then
    spring.weight = 1
    page:add(spring)
  end

  local row = ui.button(view.width - 2 * kMargin, kRow)
  if row ~= nil then
    row.background = 15
    row.border = 0
    local text = make_label(view.width - 2 * kMargin - 24, 'Back', kSmall, 'center', 'middle')
    if text ~= nil then
      text:place(12, 12)
      row:add(text)
    end
    row.on_click = function() back() end
    page:add(row)
  end

  screen = page
  ui.present('gc16')
end

back = function()
  give_back()
  console.print('about: cover')
  build_cover()
end

collect()

-- What is on the cover, said out loud: the rows are the numbers of this board and a page that lost one of
-- them is a page that lies —and a test reads this line to know which pieces the page was made of (docs/85).
console.print(string.format('about: %d pieces, %s %s on %s', #pieces, sys.name, sys.version, sys.board))
for i = 1, #pieces do
  console.print(string.format('about: piece %s %s', pieces[i].name, pieces[i].value))
end

build_cover()

-- The page is a tree of controls and the finger, the dial and the keys are the system's: what this loop has to
-- do is keep the kernel turning and offer the page again when something of it has changed. A present with
-- nothing new draws nothing (docs/30, docs/90).
while true do
  ui.present()
  sys.delay(50):await()
end
