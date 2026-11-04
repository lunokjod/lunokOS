-- menubar: a bar of menus over a page.
--   run menubar
--
-- **The bar and the menu that hangs from it are one control and it is the system's**: the app says the titles, the
-- entries of each menu and which one is unfolded, and not one pixel of it is painted here (docs/30, docs/90).
--
-- **An entry this app cannot do now is turned off and not hidden**, which is the difference with `ui.menu`: the row
-- stays where it is so the menu does not change size the day it comes back, and it is drawn in the dim tone. A dash is a
-- line between two entries, with its own air, and it is not a row a finger presses.
--
-- **A tick is a box with its middle filled**: this glass has two tones and no third one for "on", so an entry that is on
-- wears a box with something in it and one that is off wears an empty box (`View > Wrap`).
--
-- **A choice folds the menu and the app hears which title and which entry**, with the menu already folded: the rule of
-- the old system, read the action, close, and then run it. It stops by itself after forty seconds.
local caps = sys.caps()
if not caps.display then
  console.print('menubar: this board has no glass')
  return
end

local view = ui.caps()
local wrap = true

ui.title('Menu bar')

local status = ui.label(view.width - 48, 26)
status.text = 'nothing chosen yet'
status.scale = 2
status.color = 8
status:place(24, 110)

-- The three menus, built once and handed over: **an entry is a word, a table with `label`, `checked` and `enabled`, or a
-- dash** (docs/30).
local menus = {
  {title = 'File', items = {{label = 'Open'}, '-', {label = 'Save'}, {label = 'Quit', enabled = false}}},
  {title = 'Edit', items = {{label = 'Copy'}, {label = 'Paste'}}},
  {title = 'View', items = {{label = 'Wrap', checked = true}}},
}

local bar = ui.menu_bar(view.width - 40, 420)
bar:place(20, 40)
bar.bar_height = 52
bar.items = menus
bar.on_pick = function(self, title, entry)
  local word = menus[title].items[entry].label
  status.text = word .. ' (' .. menus[title].title .. ')'
  if word == 'Wrap' then
    -- **The state of the entry is the app's**: the control draws the box and the app is the one that knows whether the
    -- page wraps, so the tick is written here and the menu is handed over again (docs/90).
    wrap = not wrap
    menus[3].items[1].checked = wrap
    bar.items = menus
  end
  console.print('menubar: ' .. status.text .. ', wrap ' .. tostring(wrap))
  ui.present('gld16')
end

ui.present('gld16')
console.print('menubar: touch a word of the bar; the arrows walk the entries of the open menu')

-- **The page stays while there is somebody on it**: a bar that closes after forty seconds is a bar nobody can
-- press when the person is still reading it, and the shell takes every key after that. The way out is the shell's
-- (a Quit entry is one of the entries of this bar too) and what happened is said when the app is closed (docs/30).
function on_close()
  console.print('menubar: closed on \'' .. status.text .. '\'')
end

while true do
  sys.delay(100):await()
end
