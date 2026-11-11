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
-- **The list pages itself, and that is one place this page differs from the original** (0.166.0): the paper hid the
-- seventh library behind a sentence because its system had room for eight controls and one of them was Back. Here the
-- libraries are the **entries of a `ui.list`**, so the ones that do not fit a page are placed out of the frame by the
-- control, the dial turns the pages and the foot of the list says which one is on the glass. A library that cannot be
-- seen is a library nobody can look up, and a row called "More 7 to 12 of 40" was the app writing a list.

local caps = sys.caps()
if not caps.display then
  console.print('about: this board has no glass')
  return
end

local view = ui.caps()

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
local build_cover

-- **The screen that is on the page is given back, fills and all** (docs/30, docs/90): `release()` marks the
-- control and its fills, and the system takes the buffers when the page has been painted over.
local function give_back()
  if screen ~= nil then
    screen:release()
    screen = nil
  end
end

-- The summary under the big line: how many they are and what to do with them. **The page is not said here any more**
-- (0.166.0): the foot of the list says which page is on the glass, and two places saying it are two places that can
-- disagree (docs/30).
local function summary_line()
  if missing ~= nil then return missing end
  return string.format('%d libraries by @lunokjod. Tap one to see its details.', #libs)
end

-- **A row of the list is a control the app fills** (0.166.0; the owner: *«usa una llista i afegeix el nom de la
-- llibreria i versio a l'entrada»*): the name on the left and its version under it on the right, **inside the row's own
-- box**, and the finger or the middle key opens the detail. The rows that do not fit a page are **placed out of the
-- frame by the list itself** and the foot of the list says which page is on the glass, which is why there is no
-- "More" row and no arithmetic of pages in this file any more (docs/30, docs/90).
local function add_entry(the_list, library, index)
  local entry = ui.list_entry(view.width - 2 * kMargin, kRow)
  if entry == nil then return end

  -- **The two texts of a row are side by side and not one over the other** (0.166.0): a label paints its own box with
  -- its own paper, so a small one at the bottom of the row painted over the lower half of the name —the first attempt
  -- of this page showed the names cut in half—. The name keeps the left and the version keeps a column on the right
  -- (docs/30).
  local version_w = 96
  local name = make_label(view.width - 2 * kMargin - version_w - 36, library.name, kBody, 'left', 'middle')
  if name ~= nil then
    name:place(12, 2)
    entry:add(name)
  end

  local version = make_label(version_w, library.version, kSmall, 'right', 'middle')
  if version ~= nil then
    version:place(view.width - 2 * kMargin - version_w - 12, 2)
    entry:add(version)
  end

  entry.on_click = function() show(index) end
  the_list:add(entry)
  console.print(string.format('about: row %s %s', library.name, library.version))
  return entry
end

build_cover = function()
  -- **The page is the app's and the app owns all of it**: a control paints inside its own box, so the air
  -- around the panel would keep whatever the glass had before it —the drawing of the boot— unless the app
  -- washes the page first (docs/30, docs/50). **The wash is the page and the quality is the system's** (0.179.5): the
  -- clear says what has to be white and the present that follows says how it goes up, from what the page has drawn —a
  -- page of words is not a photograph and does not need the pass that flashes— (docs/50).
  ui.clear(15)

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

  -- **One list for every library**: the rows are entries, so each one is a stop of the dial and the finger presses it,
  -- and the list keeps the page —a page of ink and not a scroll— with its foot saying which page it is. **The room is the
  -- panel's to give** (0.174.3): `weight` takes what is left after the two lines above, so the box of the list ends at the
  -- bottom margin of the page and the rows fill it. It used to be a hand-measured height with two rows of slack, because the
  -- head measured by the app and placed by the panel disagreed by a line —a workaround of 0.167.0 that was fixed and left
  -- the list ending short of the glass— (docs/30, docs/90).
  local the_list = ui.list(view.width - 2 * kMargin, kRow)
  if the_list == nil then
    console.print('about: there is no room for the list of libraries')
  else
    the_list.weight = 1
    the_list.row_height = kRow
    page_control:add(the_list)
    local first = nil
    for i = 1, #libs do
      local entry = add_entry(the_list, libs[i], i)
      if i == 1 then first = entry end
    end
    -- **The ring is on the first row** (0.169.2): the list takes the dial when it is built, and the rows are the stops, so
    -- the page has to say which one the middle key would open —the same call the launcher makes for its first cell—.
    -- Without it the centre pressed the list and the page could only be opened with a finger (found by test_apps, 9/11).
    if first ~= nil then ui.focus(first) end
    -- **The room is the panel's**, so what the page says is the box the list has now and not a number it asked for: a case
    -- reads this line to know the page was made whole (docs/85).
    local box = the_list:caps()
    console.print(string.format('about: %d rows in a list of %d px', #libs, box.height))
  end

  screen = page_control
  ui.present()
end

-- **A row that is a button of the page** (0.167.0): the width of the page and the height of a row of the paper project,
-- with its words in the middle and the finger or the middle key pressing it. **It is here because the detail was calling
-- a name that did not exist**: the function that made the button went away with the paginator of 0.166.0 and `show` kept
-- calling it, so tapping a row of the list painted the detail and then died with `attempt to call a nil value (global
-- 'add_row')` —which is exactly what the owner met on the board: a page he could not leave and no Back in it— (docs/30).
local function add_row(parent, text, on_click)
  local button = ui.button(view.width - 2 * kMargin, kRow)
  if button == nil then return nil end

  button.background = 15
  button.border = 0
  button.on_click = on_click

  -- The words go in a label and not on the button: the ring of the focus is a band inside the border, and a text that
  -- the button drew itself would sit on it (docs/30, docs/90).
  local words = make_label(view.width - 2 * kMargin - 24, text, kBody, 'center', 'middle')
  if words ~= nil then
    words.color = 0
    words.background = 15
    words:place(12, 4)
    button:add(words)
  end

  parent:add(button)
  return button
end

-- **One library, with its name, where it lives and what it is**, and Back under it: the detail of the original.
show = function(index)
  local library = libs[index]
  if library == nil then return end

  give_back()

  -- The page is washed before the detail is built, the same as the cover (docs/30, docs/50): the clear says what is white
  -- and the present at the end says the quality, which is the system's (0.179.5).
  ui.clear(15)
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
  ui.present()

  -- **The detail is a view on top of the cover** (0.163.0): a pinch that comes from big to small gives it back and
  -- the app hears `on_back`, so the cover is what a finger finds under the detail (docs/30).
  app.view_push()
end

back = function()
  give_back()
  app.view_pop()
  console.print('about: back')
  build_cover()
end

-- **A pinch took the detail away** (0.163.0): the system has already given the view back, so this only puts the
-- cover on the glass again. It is the same page `back()` builds and it does not pop a second time (docs/30).
function on_back(depth)
  give_back()
  console.print(string.format('about: back by the gesture, %d views left', depth))
  build_cover()
end

-- --------------------------------------------------------------------- the app

read_libraries()

-- **The two lines of the head and the list are the panel's**, and the panel splits what is left with `weight` (0.174.3):
-- the count of the lines the title takes and the room it leaves were the arithmetic of 0.166.0, when the app had to size the
-- list by hand, and there is no number to keep any more (docs/30, docs/90).

-- What is on the cover, said out loud: the rows are the libraries of this build and a page that lost one of
-- them is a page that lies —and a test reads these lines to know what the page was made of (docs/85).
if missing ~= nil then
  console.print('about: ' .. missing)
else
  console.print(string.format('about: %d libraries, lunokOS %s on %s', #libs, sys.version, sys.board))
end

build_cover()

-- The page is a tree of controls and the finger, the dial and the keys are the system's: what this loop has to
-- do is keep the kernel turning and offer the page again when something of it has changed. A present with
-- nothing new draws nothing (docs/30, docs/90).
--
-- **And the page of the list is the list's** (0.179.4): a finger that slides across it turns it —left for the one after
-- and right for the one before— and so does a dial that walks past the last row of the page, and the dots of its foot say
-- which one is on the glass. It used to be read here, with a `touch.gesture()` and a `kSlidePx` of this file, because the
-- system only knew how to turn the page of a grid; a control that is used by every app is turned by the system and the app
-- only hears `on_page` (docs/30, docs/90).
while true do
  ui.present()
  sys.delay(50):await()
end
