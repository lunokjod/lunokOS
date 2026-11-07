-- typing: a text box, its caret, and the keyboard of the system typing into it.
--   run typing
--
-- **The keyboard is not a control**: it is a band the system paints at the bottom of the view, over the page, and
-- it takes the finger and the dial before the page does. The app says the box it types into (`kb.target`), shows
-- it (`kb:show`) and asks the band it takes (`kb:height()`) so its own page is the part that is left. Not one key
-- is handled here: every key arrives at the box and the box tells the app with `on_change(self, text)`, with the
-- field already written (docs/30, docs/90).
--
-- **The caret is the box's and the blink is the system's** (0.92.0): the kernel turns the block of the box the
-- keyboard types into every six hundred milliseconds and paints only that box. The app does not keep a clock, and a
-- page that only waits still shows a cursor (docs/30, docs/90).
--
-- **The enter does not type a letter**: a box of one line tells the app with `on_enter(self)`, and this page says
-- the word on the console with it.
local caps = sys.caps()
if not caps.display then
  console.print('typing: this board has no glass')
  return
end

local view = ui.caps()
ui.title('Typing')

-- The box first, because the keyboard types into it.
local box = ui.textbox(view.width - 32, 56)
box.cursor = 0
box.mask = false
box.on_change = function(self, text)
  console.print('typing: "' .. text .. '", caret at ' .. self.cursor)
  -- **The letters of the box go to the glass with a quality that cleans**: a `du` keeps a third of the ink it finds and
  -- the text of the key before stays under the new one, which on this panel is a box full of ghosts. The clean that does
  -- not blink is `gld16` (0.84.1, 0.85.2, docs/30, docs/92).
  ui.present('gld16')
end
box.on_enter = function(self)
  console.print('typing: the word is "' .. self.text .. '"')
end

-- **The keyboard of the system goes up first and the view is then what is left**: since 0.84.0 the system takes the band
-- out of the view an app is given, so `ui.caps().height` is 736 and not 912 with the keyboard on the glass. An app does
-- not have to subtract anything —and it hears `on_resize` when the band comes or goes— (docs/30, docs/90).
local kb = ui.keyboard()
kb.target = box
kb:show('alpha')
view = ui.caps()
local band = kb:height()
console.print(string.format('typing: view %d x %d, the keyboard takes %d', view.width, view.height, band))

-- The page: the whole view the app has, arranged by a panel.
local page = ui.panel(view.width, view.height, 'vertical')
page:place(0, 0)
page.padding = 16
page.gap = 12

local hint = ui.label(10, 46)
hint.text = 'Type with the keys below'
page:add(hint)
page:add(box)

local note = ui.label(10, 46)
note.text = 'The arrows walk the keys; the centre presses one'
note.color = 8
page:add(note)

ui.present('gld16')

-- **The view is another one when the band comes or goes** (0.84.0): the system calls this and the page takes the room it
-- has, which is the whole of what an app has to say about the keyboard.
function on_resize(width, height)
  page:resize(width, height)
  box:resize(width - 32, 56)
  console.print(string.format('typing: resized to %d x %d', width, height))
  ui.present('gld16')
end

-- **The blink is the system's** (0.92.0): the app has nothing to turn; it waits while the kernel keeps the block of
-- the box going and coming.
while true do
  sys.delay(1000):await()
end
