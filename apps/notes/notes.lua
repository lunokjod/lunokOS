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

local view = ui.caps()

ui.title('Notes')

local box = ui.textbox_multiline(view.width - 40, view.height - 260)
box:place(20, 60)
box.scale = 2
box.wrap = true
box.text = 'Notes\n\nThe enter key of the keyboard writes a line break in this box, and a line longer than the box '
  .. 'is broken at the last space that fits while wrap is on.'
box.cursor = 0
console.print('notes: ' .. box:rows() .. ' rows of ' .. box.scale .. 'x')

local saved = box.text
local kb = ui.keyboard()
kb.target = box
kb:show('alpha')

box.on_change = function(self, text)
  -- **The text of a control is the app's copy**: the box keeps the one it was given and hands the new one over on every
  -- key, so an app that saves or measures the note has nothing to ask the screen for (docs/30).
  saved = text
end

ui.present('gc16')
while not ui.idle() do
  sys.delay(10):await()
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
