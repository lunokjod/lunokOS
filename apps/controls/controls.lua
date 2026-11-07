-- controls: a page of the controls the system draws, arranged by a panel.
--   run controls
--
-- **Not one of these is painted in Lua.** A control is a canvas slot the kernel owns: the app sets fields,
-- says where the page goes and when it goes to the glass, and every pixel of it —the track, the knob, the
-- number, the line of a tab, the dots, the ring of the focus— is drawn by `lib/kernel/src/ui.cpp`
-- (docs/30, docs/90, docs/92).
--
-- **The page is a declaration and not a list of places**: a `ui.panel` in a column stacks its children with the
-- gap between them, stretches each one to its width and gives the room that is left to the children with a
-- `weight`. The app does not write a single coordinate of a child, so adding a line does not move the five
-- under it.
--
-- **Two controls take the arrows while they have the focus**: the strip of tabs changes its chosen tab, and the
-- slider changes its number. The field is already written when `on_change` arrives, and at the end of the range
-- the key goes back to the walk of the dial, so a page is never stuck on a control that cannot move.
--
-- Every change washes the page with `gld16`, which cleans and does not blink: a knob that moved is a ghost of the knob
-- before it, and a tick is not worth four blinks of the page (docs/50, docs/92).
local caps = sys.caps()
if not caps.display then
  console.print('controls: this board has no glass')
  return
end

local view = ui.caps()
console.print(string.format('controls: view %d x %d at %d,%d', view.width, view.height, view.x, view.y))
ui.title('Controls')

local page = ui.panel(view.width, view.height, 'vertical')
page:place(0, 0)
page.padding = 16
page.gap = 10

local heading = ui.label(10, 46)
heading.text = 'Controls'
page:add(heading)

local tabs = ui.tabs(10, 40)
tabs.items = { 'Page', 'Rows', 'About' }
tabs.index = 1
tabs.on_change = function(self, index)
  console.print('tab ' .. index)
end
page:add(tabs)

local bar_label = ui.label(10, 46)
bar_label.text = 'Progress: half of it'
page:add(bar_label)

local bar = ui.progress(10, 26)
bar.value = 50
page:add(bar)

-- **The slider**: the dial moves it, and the number and its unit are drawn by the control.
local slider = ui.slider(10, 56)
slider.value = 30
slider.min = 0
slider.max = 100
slider.step = 5
slider.suffix = '%'
slider.on_change = function(self, value)
  console.print('slider ' .. value)
end
page:add(slider)

local dots = ui.dots(10, 22)
dots.pages = 3
dots.current = 2
page:add(dots)

-- **The rows are a list**, and the list takes the room that is left: it weights itself and **pages** the rows, so
-- a page of a thousand rows costs what one page costs —on this glass a scroll is a hundred small paints and a
-- page is one—. The third row is the chosen one and it is marked, which inverts it, and the arrows of the dial
-- turn the page while the list has the focus.
local list = ui.list(10, 60)
list.weight = 1
-- **A row of a list is a line of the system plus its air**: forty-four pixels of line and twelve of air, which is what
-- the paper project gives a row (docs/92).
list.row_height = 68
list.marked = 3
list.items = {
  { title = 'Res', detail = '1 033 B', subtitle = 'the sample files of the disk' },
  { title = 'Apps', detail = '48', subtitle = 'the apps of this build' },
  { title = 'Data', detail = '4 096 B', subtitle = 'what the apps wrote' },
  { title = 'Cfg', detail = '2', subtitle = 'settings' },
  { title = 'Rtc', detail = '7 168 B', subtitle = 'the memory that survives a sleep' },
  { title = 'Nvs', detail = '12', subtitle = 'the keys the system keeps' },
}
list.on_page = function(self, page)
  console.print('list page ' .. page)
end
page:add(list)

ui.present('gld16')
console.print('controls: the page is out; the arrows move the tab, the slider and the list')

-- **The page stays while there is somebody on it.** An app that draws and returns leaves the shell in front, and
-- every key after that goes to the prompt: the controls are on the glass and none of them answers, which is what a
-- person reads as "the controls do not work" (found on the bench, 3/11). The way out is the shell's, the same as
-- every page of the system (docs/30, docs/90).
while true do
  sys.delay(100):await()
end

