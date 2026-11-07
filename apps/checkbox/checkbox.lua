-- checkbox: five UICheckboxes, the words beside each box, and a line that counts them.
--   run checkbox
--
-- A checkbox is **a row**: a square box at the left and the words that name the setting, which are a label
-- the app adds with `cb:add(label)` and places after `cb:box()`. The box is the kernel's geometry —two pixels
-- of frame, a border with the weight of the thing it is, a tick of its own colour— and the row is the
-- control's background, so the words sit on it and are composed after it (docs/30, docs/90).
--
-- **The dial walks the rows and the centre flips them**: the system focuses the first pressable control, the
-- sides move the focus, and the centre or a finger flips the box. The field `checked` is already written when
-- `on_change(self, checked)` arrives, because a checkbox's click means one thing and the system does it: an app
-- that had to flip it in a callback would be an app that can forget to. `on_activate` is not called for this
-- control.
--
-- **The third row cannot be reached with the dial** (`focusable = false`): it is a setting the app decides and
-- a person only reads, and it is here to show that the walk of the system skips it. The fourth is a dark row,
-- which is what `background`, `border` and `mark` are for.
--
-- **Every change washes the page, and the wash is `gld16`**: a tick is a few pixels of ink and a ring that moved is a ghost
-- of the ring before it, and a page of controls that leaves one is a page of grey rings. `gld16` cleans and does not blink,
-- which is the rule of this project —nothing that flickers reaches the glass— and `gc16` is the same clean with four blinks
-- of the page for a tick (docs/50, docs/92, docs/90).
local caps = sys.caps()
if not caps.display then
  console.print('checkbox: this board has no glass')
  return
end

local view = ui.caps()
console.print(string.format('checkbox: view %d x %d at %d,%d', view.width, view.height, view.x, view.y))
ui.title('Checkboxes')

-- The count of the settings that are on, in the biggest letters the system face has: the row is the box, the
-- words are a child, and this line is what a person reads after the dial has walked the page.
local summary = ui.label(view.width - 40, 46)
summary.valign = 'middle'
summary:place(20, 16)

local rows = {}
local wash = false

local function count_on()
  local on = 0
  for i = 1, #rows do
    if rows[i].row.checked then on = on + 1 end
  end
  return on
end

local function say_the_count()
  summary.text = string.format('%d of %d on: the box is the setting', count_on(), #rows)
end

-- **A row is a checkbox plus a label**, and the label starts where the box ends: `cb:box()` is the number of
-- pixels the box takes, and it is the kernel's and not a field, so an app cannot draw one thing and mark
-- another.
local function add_row(name, at_y, checked, focusable, dark)
  local row = ui.checkbox(view.width - 40, 56)
  row:place(20, at_y)
  row.checked = checked
  row.focusable = focusable
  if dark then
    row.background = 0
    row.border = 15
    row.mark = 15
  end
  local words = ui.label(view.width - 40 - row:box() - 16, 40)
  words.text = name
  words.valign = 'middle'
  words.background = row.background
  words.color = dark and 15 or 0
  words:place(row:box() + 12, 8)
  row:add(words)
  row.on_focus = function(self, on)
    if on then console.print('focus ' .. name) end
    wash = true  -- the ring moved: the page is washed, or the ring before it stays as a ghost
  end
  row.on_change = function(self, on)
    console.print(string.format('change %s %s', name, tostring(on)))
    say_the_count()
    wash = true
  end
  rows[#rows + 1] = {row = row, words = words}
  return row
end

add_row('The clock', 72, true, true, false)
add_row('The battery', 144, false, true, false)
add_row('Chosen by the app', 216, true, false, false)
add_row('A dark row', 288, false, true, true)

-- **The way out is a control and not a gesture**: the centre is Enter on a page with controls, so there is no
-- three-tap exit and the app offers a Quit button, which is what the guide says to do (docs/30, docs/90).
local done = false
local quit = ui.button(200, 72)
quit:place(20, 380)
local quit_words = ui.label(180, 48)
quit_words.text = 'Quit'
quit_words.align = 'center'
quit_words.valign = 'middle'
quit_words:place(10, 12)
quit:add(quit_words)
quit.on_click = function(self)
  done = true
end
quit.on_focus = function(self, on)
  if on then console.print('focus Quit') end
  wash = true
end

say_the_count()
console.print(string.format('checkbox: a row of 56 holds a box of %d px', rows[1].row:box()))
ui.present('gld16')  -- the first page, washed: nothing of the page before it
while not done do
  sys.delay(50):await()
  if wash then
    wash = false
    ui.present('gld16')
  end
end
console.print(string.format('checkbox: %d of %d on at the end', count_on(), #rows))
