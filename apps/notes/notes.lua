-- notes: a page you type into, with the keyboard of the system.
--   run notes
--
-- **The box is the system's and the typing is too**: the app says the text, where the caret is in bytes and whether the
-- long lines wrap, and the system shows its keyboard, takes the band at the bottom of the page and writes into the box
-- the app pointed at (docs/30, docs/90).
--
-- **The enter key writes a line break here**, which is the one thing this box does that `ui.textbox` does not: in a box
-- of one line the enter ends the edition and the app hears `on_enter`; in a box of several lines it is the key that
-- starts a new line, and the keyboard of the system paints it for this box.
--
-- **The window follows the caret**: a text longer than the box does not scroll by itself, the rows that are shown are the
-- ones the caret is in, so a person typing at the end of a document always sees what they are writing (docs/90).
--
-- It stops by itself after forty seconds, so a script can run it.
local caps = sys.caps()
if not caps.display then
  console.print('notes: this board has no glass')
  return
end

ui.title('Notes')

local box = ui.textbox_multiline(100, 50)
box.scale = 2
box.wrap = true
box.text = 'Notes\n\nThe enter key of the keyboard writes a line break in this box, and a line longer than the box '
  .. 'is broken at the last space that fits while wrap is on.'
box.cursor = 0

local saved = box.text
-- **The keyboard first, because the box gets what the view has left over it** (0.84.0): with the band up, `ui.caps()` says
-- 736 and not 912, so the page does not have to subtract the keyboard from anything.
local kb = ui.keyboard()
kb.target = box
kb:show('alpha')
local view = ui.caps()
box:resize(view.width - 40, view.height - 76)
box:place(20, 60)
console.print('notes: ' .. box:rows() .. ' rows of ' .. box.scale .. 'x, view ' .. view.height)

box.on_change = function(self, text)
  -- **The text of a control is the app's copy**: the box keeps the one it was given and hands the new one over on every
  -- key, so an app that saves or measures the note has nothing to ask the screen for (docs/30).
  saved = text
  -- **A page that types has to paint what it typed, and with a quality that cleans**: a `du` leaves a third of the ink
  -- it finds, so the letters of the key before stay under the new ones and the box fills with ghosts. **`gld16`, which
  -- cleans without blinking** (0.85.2): it is 450 ms and no flash, and `gl16` —which the app asked for until now— is the
  -- same 450 ms with a blink of the page (the rule is the one this project wrote down for itself, docs/92).
  ui.present('gld16')
end

ui.present('gld16')
while not ui.idle() do
  sys.delay(10):await()
end

-- **The box follows the view** when the band comes or goes: the system says the page is another one and the app fits the
-- box into it (0.84.0, docs/30, docs/90).
function on_resize(width, height)
  box:resize(width - 40, height - 76)
  ui.present('gld16')
end
console.print('notes: type with the keyboard, or walk the keys with the dial')

-- **The page stays while there is somebody typing on it**: a box that hides its keyboard after forty seconds takes the
-- keyboard away in the middle of a word, and the shell takes every key after that. What was written is said when the
-- app is closed (docs/30, docs/90).
function on_close()
  console.print('notes: closed with ' .. #saved .. ' bytes')
end

while true do
  sys.delay(100):await()
end
