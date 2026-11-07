-- gallery: every control the system draws, eight pages of them, in the typeface of the paper.
--   run gallery [page]
--
-- **The size is the point, and it is measured** (5/11, the owner with the board in his hand: *«tot el UI actual de
-- lunokOS és massa petit… no vull reinterpretacions, vull la mateixa estètica i tipus de lletra»*). This glass is
-- 540 x 960 at **235 pixels per inch**: the face of the system is five by seven pixels, so a label at `scale` 2 draws a
-- **14 px line**, which is 1,5 mm — a stamp and not a text. The paper project tried 16 px and then 29 and both were too
-- small; what it settled on is **43 px of line** (4,7 mm, a pocket book), and that is the most that fits: twenty
-- characters of bold at 43 px take 440 of the 516 px of useful width (docs/30, docs/92).
--
-- **The same typeface is on this disk**: `/res/fonts/dejavusans-bold.ttf`, eight and a half kilobytes of outlines. A face
-- of outlines is drawn at `8 * scale` pixels of body (`kOutlineBody`), so the sizes of this page are the ones of the
-- paper: **3 = 24 px of body (33 px of line, the dense text and the tabs), 4 = 32 px (43 px of line, THE TEXT), 5 = 40 px
-- (57 px, a title) and 7 = 56 px (82 px, a big number)**. Only `scale` changes: the face is one, and the drawing is the
-- letter redrawn at every size and not a table of pixels stretched (docs/30).
--
-- **Not one of these is painted in Lua.** A control is a slot the kernel owns: the app sets fields and says when the
-- page goes to the glass, and every pixel —the frame, the tick, the track, the knob, the ring of the focus, the balls of
-- the paginator— is drawn by `lib/kernel/src/ui.cpp` (docs/30, docs/90, docs/92).
--
-- **The tabs at the top choose the page and the dots at the bottom say which one it is**, so the two controls that are
-- about "where am I" are the navigation of this app: the dial moves the tabs, `on_change` builds the page, and the strip
-- follows. A page that is left is given back whole —every control of it, children first— (docs/90).
--
-- Every change washes the page with `gld16`, which cleans and does not blink (docs/50, docs/92).
local caps = sys.caps()
if not caps.display then
  console.print('gallery: this board has no glass')
  return
end

local view = ui.caps()
console.print(string.format('gallery: view %d x %d at %d,%d', view.width, view.height, view.x, view.y))
ui.title('Gallery')

-- **The sizes of the paper, which is all this page says**: the system's face IS the bold one of the disk since 0.96.0
-- (the kernel loads it at boot), so an app asks for a size and never loads a file —`8 * scale` pixels of body, and
-- 4 = 32 px is the 43 px line of the paper project— (docs/30, docs/92).
local body_scale = 4
local small_scale = 3
local title_scale = 5
-- 43 px of line plus the air a label needs above and below it.
local kBodyLine = 46
local kRow = 72
local kButton = 72

local kPages = 5
-- **Five titles, which is what a strip of tabs holds on this glass**: the paper project's settings page has five
-- —"Screen Media Input Home Device"— and each column is `view.width / 5`, a hundred and eight pixels, which is a word
-- of five letters at the dense size. Eight columns would be sixty-seven pixels each, and a word that does not fit is
-- cut and not wrapped: the strip said "T... N... R..." and that is not a tab, it is a broken word (found by the owner
-- on the bench, 5/11, docs/92).
local kNames = { 'Text', 'Nums', 'Rows', 'Form', 'Menu' }

local root = ui.panel(view.width, view.height, 'vertical')
root:place(0, 0)
root.padding = 12
root.gap = 12

local tabs = ui.tabs(10, 52)
tabs.items = kNames
tabs.index = 1
tabs.scale = small_scale
root:add(tabs)

-- **The page that is on the glass**: a panel that is emptied and built again, and the room that is left is
-- its own (`weight`), so the tabs, the dots and the hint keep theirs whatever the page carries.
local body = ui.panel(10, 100, 'vertical')
body.weight = 1
body.gap = 12
root:add(body)

local dots = ui.dots(10, 26)
dots.pages = kPages
dots.current = 1
root:add(dots)

local hint = ui.label(10, 36)
hint.scale = small_scale
hint.text = 'the arrows move the dial, the centre presses'
root:add(hint)

-- Every control of the page that is on the glass, in the order it was made: children are given back before
-- their father, because a control that is inside another one outlives it otherwise.
local built = {}
local first_stop = nil

local function keep(control, name)
  if control == nil then return nil end
  built[#built + 1] = control
  body:add(control)
  -- **The first control of the page a person can land on**, which is where the dial goes when the page is a new one:
  -- a label is not a place the dial stops, a button is (docs/30, docs/90).
  if first_stop == nil and control.focusable == true then first_stop = control end
  -- **A control that is pressed says so on the line at the bottom** (8/11): the ring and the press itself are
  -- painted by the system, but what a press *does* is the app's, and a page whose button answers nothing is a page
  -- a person presses twice and then calls dead (the owner on the board: *«no respon a l'usuari»*, docs/92). The
  -- hooks the app wrote for itself (`on_change`, `on_pick`, `on_open`) keep their own answer.
  if name ~= nil and control.on_click == nil then
    control.on_click = function(self)
      hint.text = 'pressed ' .. name
      console.print('pressed ' .. name)
    end
  end
  return control
end

local function clear_page()
  for i = #built, 1, -1 do
    built[i]:release()
  end
  built = {}
  first_stop = nil
  -- **And the whole page the app is given is paper before the new one is built**: a control paints its own rectangle and
  -- no more, so a page of three controls after a page of six would leave the three of the one before on the glass —the
  -- page was a mix of two pages after a tab—. The page is the app's and the bar is not in it (docs/30, docs/90).
  ui.clear(15)
end

-- **The words that name a control are the dense text of the paper project** (scale 3 = 24 px of body, the 33 px line)
-- and not scale 2: a page whose captions are half the size of the text is a page of a manual and not the page of a
-- system (found on the bench, 5/11, docs/92).
local function label(text, height, scale)
  local line = keep(ui.label(10, height or 34))
  line.text = text
  line.scale = scale or small_scale
  return line
end

-- A page is a function that fills `body`. The eight of them cover the twenty controls of the system, and each
-- one names the control it shows: this app is also the page a person reads to know what there is.
local pages = {}

pages[1] = function()
  label('label: text, cut with three dots', 40)
  local row = keep(ui.row(10, 88), 'row')
  row.title = 'row'
  row.detail = '124 B'
  row.subtitle = 'title, detail and the line of the separator'
  row.separator = true
  row.marked = true
  row.scale = body_scale
  local button = keep(ui.button(10, kButton), 'button')
  button.background = 15
  button.border = 0
  -- **A child is not stretched by the panel**: a label inside a button keeps the width it was made with, and one of ten
  -- pixels is a word cut to two dots (found on the bench, 5/11).
  local words = ui.label(view.width - 64, kBodyLine)
  words.text = 'button: a frame with volume'
  words.scale = body_scale
  button:add(words)
  built[#built + 1] = words
  local box = keep(ui.checkbox(10, 56), 'checkbox')
  box.checked = true
  box.scale = body_scale
  local tick = ui.label(10, kBodyLine)
  tick.text = 'checkbox: the mark and not a fill'
  tick.scale = body_scale
  box:add(tick)
  built[#built + 1] = tick
end

pages[2] = function()
  label('progress: 40 of 100, and unknown', 40)
  local bar = keep(ui.progress(10, 32), 'progress')
  bar.value = 40
  local unknown = keep(ui.progress(10, 32), 'unknown')
  unknown.value = -1
  local slider = keep(ui.slider(10, 76))
  slider.value = 45
  slider.min = 0
  slider.max = 100
  slider.step = 5
  slider.suffix = '%'
  slider.scale = title_scale
  slider.on_change = function(self, value) console.print('slider ' .. value) end
  local spark = keep(ui.sparkline(10, 72), 'sparkline')
  spark.samples = { 12, 18, 9, 24, 20, 31, 28, 33 }
  local pie = keep(ui.pie(10, 132), 'pie')
  pie.slices = { 5, 3, 2 }
end

pages[3] = function()
  label('list: rows in pages', 40)
  local list = keep(ui.list(10, 80))
  list.weight = 1
  list.row_height = kRow
  list.marked = 2
  list.scale = body_scale
  list.items = {
    { title = 'Res', detail = '1 033 B', subtitle = 'the sample files of the disk' },
    { title = 'Apps', detail = '48', subtitle = 'the apps of this build' },
    { title = 'Data', detail = '4 096 B', subtitle = 'what the apps wrote' },
  }
  list.on_page = function(self, page) console.print('list page ' .. page) end
  label('notification_bar: cards', 40)
  local band = keep(ui.notification_bar(10, 150))
  band.scale = body_scale
  band.items = { { title = 'Battery', text = 'low, 20%' }, { title = 'Alarm', text = 'in ten minutes' } }
  band.on_pick = function(self, item) console.print('notice ' .. item.title) end
  label('month: six weeks, always', 40)
  local cal = keep(ui.month(10, 250))
  cal.year = 2026
  cal.month = 11
  cal.selected = 5
  cal.today = 5
  cal.weekdays = { 'Mo', 'Tu', 'We', 'Th', 'Fr', 'Sa', 'Su' }
  cal.scale = small_scale
  cal.on_pick = function(self, day) console.print('day ' .. day) end
end

pages[4] = function()
  label('textbox: one line and a caret', 40)
  local line = keep(ui.textbox(10, 56), 'textbox')
  line.text = 'tap me and the keyboard comes'
  line.cursor = 3
  line.scale = body_scale
  label('textbox_multiline: it wraps', 40)
  local field = keep(ui.textbox_multiline(10, 96), 'box of lines')
  field.weight = 1
  field.text = 'A box of several lines: the text wraps, the finger puts the caret where it touches.'
  field.wrap = true
  field.top = -1
  field.scale = body_scale
  label('selector: chosen, not typed', 40)
  local picker = keep(ui.selector(10, 64))
  picker.label = 'Length'
  picker.items = { 'no end', '5 minutes', 'one hour' }
  picker.index = 1
  picker.empty = 'no end'
  picker.scale = body_scale
  picker.on_open = function(self) console.print('selector: the app puts a menu over it') end
end

pages[5] = function()
  label('menu_bar: titles and a drop', 40)
  local bar = keep(ui.menu_bar(10, 210))
  bar.scale = body_scale
  bar.items = {
    { title = 'File', items = { { label = 'Save' }, { label = 'Close' }, '-' } },
    { title = 'View', items = { { label = 'Grid', checked = true }, { label = 'List' } } },
  }
  bar.open = 1
  bar.index = 1
  bar.on_pick = function(self, title, entry) console.print('menu ' .. title) end
  label('menu: a card of options', 40)
  local menu = keep(ui.menu(10, 120))
  menu.weight = 1
  menu.scale = body_scale
  menu.items = { { label = 'Rename', tag = 'F2' }, { label = 'Delete', tag = 'F8' }, { label = 'Info', primary = true } }
  menu.index = 1
  menu.on_pick = function(self, index) console.print('menu picked ' .. index) end
end

local shown = 0
local function show(page)
  if page < 1 then page = 1 end
  if page > kPages then page = kPages end
  if page == shown then return end
  shown = page
  clear_page()
  tabs.index = page
  dots.current = page
  pages[page]()
  -- **And the dial goes into the page and not back to the strip**: the page was built again, so the control the dial was
  -- on is gone and the walk would start at the first one of the tree —the tabs—, where the arrows change the tab for
  -- ever and nothing of the page can be pressed (the owner, 7/11: *«no puc pitjar res»*) (docs/30).
  if first_stop ~= nil then ui.focus(first_stop) end
  -- **The page that is on the glass is said out loud**, because a gallery whose pages do not turn is a page that looks
  -- right and is the first one for ever —and the console is where a test and a person read it— (docs/30).
  console.print('page ' .. page .. ' ' .. kNames[page])
  ui.present('gld16')
end

-- The tabs are the navigation, so the dial moves them and this says which page that was.
tabs.on_change = function(self, index)
  show(index)
end

-- **The page a person asks for** (`run gallery 5`): the word of `run` arrives as a path and here it is the number of a
-- page, so a gallery can be opened where it is wanted instead of turned to —and a test can ask for every page of it
-- without a finger— (docs/30, docs/40).
--
-- **`app.args` is not there when `run` said nothing**, and neither is the path: it is a table that exists when there is
-- an argument, so the first of the three questions is the table itself. `run gallery` with no word is the way a person
-- opens this app, and it was the one that answered `attempt to index a nil value (field 'args')` on the board (found by
-- the owner, 5/11) because the suite only ever ran it as `run gallery <n>`.
local first = 1
if app ~= nil and app.args ~= nil and app.args.path ~= nil then
  first = tonumber(app.args.path) or 1
end
show(first)
console.print('gallery: ' .. kPages .. ' pages, 20 controls, the face of the paper at ' .. body_scale)

-- **The page stays while there is somebody on it.** An app that draws and returns leaves the shell in front
-- and every key after that goes to the prompt: the controls are on the glass and none of them answers, which
-- is what a person reads as "the controls do not work" (docs/30, docs/90).
while true do
  sys.delay(100):await()
end
