-- about: who is behind this, and what it is made of — the port of the paper project's About (0.122.0, 0.124.0).
--
-- **Two screens, as the original**: a cover with one big line, the count and the list of the libraries, and
-- the detail of one library, which opens with a finger and comes back with Back.
--
-- **The list is the libraries of this project, and it is generated.** `tools/gen_about.py` reads the
-- `library.json` of every folder of `lib/` —the same file PlatformIO reads— and writes `res/libs.txt`, which is
-- what this page lists: the name, the version, the folder, the licence of the few that have one of their own
-- and the sentence the library says about itself. A list typed into an app is a list that starts lying the day
-- a library is added; this one cannot disagree with the build it is running on (docs/45).
--
-- **And if the file is not there, the page says so** instead of showing an empty list: what cannot be seen has
-- to be explainable too, which is the original's rule.
--
-- **There is no `visible` in this system** (docs/30, docs/90), so a screen is built and given back and not
-- hidden: opening a library returns the cover to the system and builds the detail, and Back does the other way
-- round. It is the same two screens of the original, said the way this system has them —and `release()` takes
-- the fills of a control with it, so giving back the panel gives back the whole screen—.
--
-- **The sizes are the paper project's, in pixels of line** —`small` 33, `body` 43 and `big` 82—, asked through
-- `ui.line_height` as the calculator does, and a text that does not fit a label ends in "..." here: so the
-- sentences are broken in this file, with the same measurement the label draws with (docs/30, docs/90).
--
-- **The rest of the page is the original's numbers too** (docs/45): the padding of 20 and the gaps of 12 and
-- 14 of its two panels, the buttons with the body font and the height of a button of the paper project —the
-- line of the body plus twice its vertical padding, `lineHeight() + 2 * padY`—, the text centred inside them,
-- and the four lines of the detail in the order the original wrote them. The icon the manifest names is the
-- drawing of the paper project: `res/icon-source.png` is the master a person edits and `res/icon.png` is what
-- `tools/make_icon.py` makes of it.
--
-- **The list pages, and that is the one place this page differs from the original**: the paper hid the seventh
-- library behind a sentence because its system has room for eight controls and one of them is Back. Here every
-- library of the project has to be reachable, so the last row walks to the next page and says which libraries
-- it holds. A library that cannot be seen is a library nobody can look up.

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
local kBody = scale_for(43)
local kBig = scale_for(82)

-- The height of a line of each size, in pixels, which is what the arithmetic of the page needs.
local function line(scale)
  return scale * ui.line_height(1)
end

-- **The padding and the gaps of the original**, one for the list and one for the detail: `list:set_padding(20)`
-- and `list:set_gap(12)`, `detail:set_padding(20)` and `detail:set_gap(14)` (docs/45).
local kMargin = 20
local kListGap = 12
local kDetailGap = 14

-- **The height of a button is the paper project's arithmetic**: the line of the body font plus twice the
-- vertical padding of its button (`f.lineHeight() + 2 * padY`, with `padY_ = 10`), which is 63 px there and 64
-- here, where a step of this face is eleven pixels and the body is forty-four (docs/45).
local kRow = line(kBody) + 20

-- The libraries, read from `res/libs.txt`, and where the list is.
local libs = {}
local pages = 1
local page = 1
local per_page = 1
local missing = nil
-- The control of the screen that is on the page, which is given back when the other one is built.
local screen = nil

-- **The lines a text takes in a label of that width**: a label of this system does not wrap —a text that does
-- not fit ends in "..."— so the app breaks it, and it measures with the same call the label draws with
-- (docs/30, docs/90).
local function broken(text, width, scale)
  local lines = {}
  local line_text = ''

  for word in text:gmatch('%S+') do
    local try = (line_text == '') and word or (line_text .. ' ' .. word)

    if line_text ~= '' and ui.text_width(try, scale) > width then
      lines[#lines + 1] = line_text
      line_text = word
    else
      line_text = try
    end
  end

  if line_text ~= '' then lines[#lines + 1] = line_text end

  return lines
end

local function make_label(width, text, scale, align, valign)
  local label = ui.label(width, line(scale) + 6)
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

  -- **An empty sentence is still a line of the page**: the original's third label of the detail is empty when a
  -- library has no address, and the page keeps its room so that the shape is the same (docs/45).
  if #lines == 0 then lines = {''} end

  for i = 1, #lines do
    local label = make_label(width, lines[i], scale, align)
    if label ~= nil then
      parent:add(label)
      made = made + 1
    end
  end

  return made
end

-- ------------------------------------------------------------------ the data

-- **The list is data and not a chunk**: `dofile`, `loadfile` and `load` are removed from an app's state on
-- purpose (docs/20, docs/30), so `res/libs.txt` is one library a line, a tab between fields and the sentence at
-- the end of the line —which is also cheaper than compiling a module for a page of five fields— (docs/45).
local function read_libraries()
  libs = {}

  local text = fs.read('/res/libs.txt')
  if text == nil then
    missing = 'libs.txt is missing: run tools/gen_about.py'
    return
  end

  for line in text:gmatch('[^\n]+') do
    if line:sub(1, 1) ~= '#' then
      local name, version, dir, licence, what = line:match('^([^\t]*)\t([^\t]*)\t([^\t]*)\t([^\t]*)\t(.*)$')
      if name ~= nil then
        libs[#libs + 1] = {name = name, version = version, dir = dir, license = licence, what = what}
      end
    end
  end

  if #libs == 0 then
    missing = 'libs.txt has no libraries in it'
  end
end

-- ----------------------------------------------------------------- the screens

local show -- the detail, which the cover's rows call
local back -- the cover again, which the detail's button calls
local next_page -- the next page of the list, which the More row calls
local build_cover

-- **The screen that is on the page is given back, fills and all** (docs/30, docs/90): `release()` marks the
-- control and its fills, and the system takes the buffers when the page has been painted over.
local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
end

-- A row of the cover: a frame a finger can press, with the words centred inside it. The label is set in and
-- not on the frame, because the ring of the focus is a pause and a band inside the border (docs/30).
local function add_row(page_control, text, on_click)
  local row = ui.button(view.width - 2 * kMargin, kRow)
  if row == nil then return end

  row.background = 15
  row.border = 0

  local label = make_label(view.width - 2 * kMargin - 24, text, kBody, 'center', 'middle')
  if label ~= nil then
    label:place(12, 12)
    row:add(label)
  end

  row.on_click = on_click
  page_control:add(row)
end

-- The summary under the big line: how many, what to do with them, and on which page you are.
local function summary_line()
  if missing ~= nil then return missing end

  if pages > 1 then
    return string.format('%d libraries by @lunokjod. Page %d of %d: tap one, or More for the next ones.', #libs,
      page, pages)
  end

  return string.format('%d libraries by @lunokjod. Tap one to see its details.', #libs)
end

build_cover = function()
  -- **The page is the app's and the app owns all of it**: a control paints inside its own box, so the air
  -- around the panel would keep whatever the glass had before it —the drawing of the boot— unless the app
  -- washes the page first (docs/30, docs/50).
  ui.clear(15, 'gc16')

  local page_control = ui.panel(view.width, view.height, 'vertical')
  page_control.padding = kMargin
  page_control.gap = kListGap
  page_control:place(0, 0)

  -- One big line: the name of the system and its version, which is what a person wants to read first on a page
  -- called About. The original put its motto here; a motto is not mine to write. **It is broken like any other
  -- text**: at the size of the original a name and a version do not fit one line of this glass, and a line that
  -- ends in "lunokOS 0..." is a title nobody can read (0.124.0).
  add_lines(page_control, view.width - 2 * kMargin, sys.name .. ' ' .. sys.version, kBig, 'center')

  add_lines(page_control, view.width - 2 * kMargin, summary_line(), kSmall)

  local first = (page - 1) * per_page + 1
  local last = math.min(#libs, first + per_page - 1)

  for i = first, last do
    local library = libs[i]
    add_row(page_control, library.name .. '   ' .. library.version, function() show(i) end)
    console.print(string.format('about: row %s %s', library.name, library.version))
  end

  if pages > 1 then
    if page < pages then
      local upto = math.min(#libs, last + per_page)
      add_row(page_control, string.format('More   %d to %d of %d', last + 1, upto, #libs), function() next_page() end)
    else
      add_row(page_control, string.format('First page   1 to %d of %d', per_page, #libs), function() next_page() end)
    end
  end

  console.print(string.format('about: cover page %d of %d', page, pages))

  screen = page_control
  ui.present('gc16')
end

-- **One library, with its name, where it lives and what it is**, and Back under it: the detail of the original.
show = function(index)
  local library = libs[index]
  if library == nil then return end

  give_back()

  -- The page is washed before the detail is built, the same as the cover (docs/30, docs/50).
  ui.clear(15, 'gc16')
  console.print(string.format('about: open %s %s', library.name, library.version))

  local page_control = ui.panel(view.width, view.height, 'vertical')
  page_control.padding = kMargin
  page_control.gap = kDetailGap
  page_control:place(0, 0)

  -- The four lines of the original, in its order: the subject, the credits, the address and what it is. The
  -- credits here are the folder and the licence, which is what this tree says of a library; the address is
  -- empty because a library of this project has none to give.
  add_lines(page_control, view.width - 2 * kMargin, library.name .. '   ' .. library.version, kSmall)
  local credits = library.dir or ''
  if library.license ~= nil and library.license ~= '' then
    credits = credits .. '  -  ' .. library.license
  end
  add_lines(page_control, view.width - 2 * kMargin, credits, kSmall)
  add_lines(page_control, view.width - 2 * kMargin, '', kSmall)
  add_lines(page_control, view.width - 2 * kMargin, library.what or '', kSmall)

  -- A cell that takes what is left, which is what pushes the button to the bottom of the page.
  local spring = ui.panel(10, 10, 'vertical')
  if spring ~= nil then
    spring.weight = 1
    page_control:add(spring)
  end

  add_row(page_control, 'Back', function() back() end)

  screen = page_control
  ui.present('gc16')
end

back = function()
  give_back()
  console.print('about: back')
  build_cover()
end

next_page = function()
  page = page + 1
  if page > pages then page = 1 end

  give_back()
  console.print(string.format('about: page %d', page))
  build_cover()
end

-- --------------------------------------------------------------------- the app

read_libraries()

-- **How many rows fit** is the arithmetic of this glass: what is left after the padding, the big line (as many
-- lines as it took) and the summary, and one of those rows is the More when the list has more than one page.
local motto_lines = math.max(1, #broken(sys.name .. ' ' .. sys.version, view.width - 2 * kMargin, kBig))
-- The summary is given **two lines** whatever this page turns out to say: the long form ("Page 1 of 9: tap one,
-- or More for the next ones") is what decides the room, and the short one fits inside it. Taking it from the
-- strings of the moment would size the page of the first page and no other.
local summary_lines = 2
local used = 2 * kMargin + motto_lines * (line(kBig) + 6) + summary_lines * (line(kSmall) + 6) + 3 * kListGap
local slots = math.max(2, math.floor((view.height - used + kListGap) / (kRow + kListGap)))

if #libs > slots then
  per_page = slots - 1
else
  per_page = math.max(1, #libs)
end
pages = math.max(1, math.ceil(#libs / per_page))

-- What is on the cover, said out loud: the rows are the libraries of this build and a page that lost one of
-- them is a page that lies —and a test reads these lines to know what the page was made of (docs/85).
if missing ~= nil then
  console.print('about: ' .. missing)
else
  console.print(string.format('about: %d libraries, lunokOS %s on %s, %d rows a page', #libs, sys.version,
    sys.board, per_page))
end

build_cover()

-- The page is a tree of controls and the finger, the dial and the keys are the system's: what this loop has to
-- do is keep the kernel turning and offer the page again when something of it has changed. A present with
-- nothing new draws nothing (docs/30, docs/90).
while true do
  ui.present()
  sys.delay(50):await()
end
