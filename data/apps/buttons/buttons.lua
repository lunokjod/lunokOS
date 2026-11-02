-- buttons: eight UIButtons, each with a UILabel inside, and every one answers a press.
--   run buttons
--
-- A button is a container with a frame: it paints a background and a border, and a child label is added
-- with `button:add(label)` and composed after it, clipped to the same frame (docs/30, docs/90).
--
-- **The dial walks them and the centre presses them**: the system focuses the first pressable control, the
-- sides move the focus, the centre activates it, and a finger does the same. `on_click` is what a press
-- does, `on_activate` is the same press under the other name, and `on_focus` says the ring arrived. Each
-- button counts its own presses in its own label, so a click is **one** label that changes.
--
-- The counts on the console are the other half: a present with nothing new draws nothing, a label that
-- changes draws only itself, and a button that changes draws its frame and asks its child to draw again.
-- The last lines count a click.
--
-- **It is under eight kilobytes on purpose**: the system reads an app's entry into one block of that size
-- (docs/75), and an example that does not fit is not an example.
local caps = sys.caps()
if not caps.display then
  console.print('buttons: this board has no glass')
  return
end

local view = ui.caps()
console.print(string.format('buttons: view %d x %d at %d,%d', view.width, view.height, view.x, view.y))
ui.title('Buttons')

-- **The face of the page is a `.lfont` made from Nunito**, a rounded sans cut to the printable ascii by
-- `tools/ttf_to_lfont.py`: sixteen by sixteen pixels of cell, upper and lower case, and **a table that is drawn
-- without decoding anything** (that is what a `.lfont` is and why it is the fast one). Its licence travels with
-- it (`/res/LICENCE-nunito.txt`), and so does the tool that would make another one (docs/30).
local face, face_err = ui.load_font('/res/ui-16.lfont')
if face == nil then
  console.print('buttons: no face: ' .. tostring(face_err and face_err.message))
end
if face ~= nil then
  local w, h, glyphs = face:size()
  console.print(string.format('buttons: face %dx%d, %d glyphs, "Handgloves" is %d px', w, h, glyphs,
    face:text_width('Handgloves', 1)))
end
local button_draws = 0
local label_draws = 0
local clicks = 0
local focuses = 0
local buttons = {}
local labels = {}

-- **What a button does when it is pressed**: it counts its presses in its own label, which is one label
-- that changes. `on_focus` is the other half of the contract, and the ring itself is the system's to draw.
local function attach(button, label, spec, how)
  local hook = function(self)
    clicks = clicks + 1
    label.text = spec.text .. ' ' .. clicks
    console.print(string.format('buttons: %s on "%s", %d presses', how, spec.text, clicks))
  end
  -- A control with no `on_click` gets `on_activate`: it is the same press under the other name.
  if how == 'activate' then button.on_activate = hook else button.on_click = hook end
  button.on_focus = function(self, focused)
    if not focused then return end
    focuses = focuses + 1
    console.print(string.format('buttons: focus on "%s", %d moves', spec.text, focuses))
  end
end
local function make_button(spec)
  local button, err = ui.button(spec.w, spec.h)
  if button == nil then
    console.print(string.format('buttons: no room for button %d: %s', #buttons + 1,
      tostring(err and err.message)))
    return nil
  end
  button.background = spec.background
  button.border = spec.border
  button.pressed = spec.pressed or false
  button:place(spec.x, spec.y)
  local native = button.on_draw
  button.on_draw = function(self)
    button_draws = button_draws + 1
    native(self)
  end
  buttons[#buttons + 1] = button
  return button
end

local function add_label(button, spec)
  local label, err = ui.label(spec.w, spec.h)
  if label == nil then
    console.print(string.format('buttons: no room for label: %s', tostring(err and err.message)))
    return nil
  end
  label.text = spec.text
  label.color = spec.color
  label.background = spec.background
  label.align = 'center'
  label.valign = 'middle'
  label.scale = spec.scale
  if label:font(spec.face) == nil then
    console.print(string.format('buttons: "%s" has no face', spec.text))
  end
  label:place(10, 10)
  button:add(label)
  local native = label.on_draw
  label.on_draw = function(self)
    label_draws = label_draws + 1
    native(self)
  end
  labels[#labels + 1] = label
  return label
end

local specs = {
  {x = 10, y = 90, w = 250, h = 150, text = 'One'},
  {x = 280, y = 90, w = 250, h = 150, text = 'Two'},
  {x = 10, y = 260, w = 250, h = 150, text = 'Three'},
  {x = 280, y = 260, w = 250, h = 150, text = 'Four', inverted = true},
  {x = 10, y = 430, w = 250, h = 150, text = 'Five'},
  {x = 280, y = 430, w = 250, h = 150, text = 'Six'},
  {x = 10, y = 600, w = 250, h = 150, text = 'Seven'},
  {x = 280, y = 600, w = 250, h = 150, text = 'Eight', inverted = true},
}

for i = 1, #specs do
  local spec = specs[i]
  -- Black on white, or white on black for the inverted ones: **no grey at all** in the frame or the word.
  spec.background = spec.inverted and 0 or 15
  spec.border = spec.inverted and 15 or 0
  spec.color = spec.inverted and 15 or 0
  -- **The cell is sixteen pixels, so scale one is already a big letter**: the face is drawn at the size it was
  -- cut at and scaling it would be a multiply of a picture and not a better letter (docs/30).
  spec.scale = 1
  spec.face = face
end

ui.clear(15, 'gc16')
ui.text(20, 12, 'UIBUTTON', 4, 0)
ui.text(20, 52, 'A FRAME THAT HOLDS A LABEL', 1, 8)
ui.rect(20, 74, view.width - 40, 2, 0, 'gc16')
ui.rect(0, 84, view.width, 686, 13, 'gc16')

for i = 1, #specs do
  local spec = specs[i]
  local button = make_button(spec)
  if button ~= nil then
    local label = add_label(button, {w = 230, h = 130, text = spec.text, background = spec.background,
                                     color = spec.color, scale = spec.scale, face = spec.face})
    -- The fourth button answers `on_activate` instead of `on_click`, so the page shows both names.
    if label ~= nil then
      attach(button, label, spec, (i == 4) and 'activate' or 'click')
      -- **The word goes down with the button.** A frame that sinks and a word that does not is a picture of a
      -- button and not a button: while it is held the label is moved three pixels down and to the right, and
      -- the button draws itself before its children are composed, so the word moves in the same page
      -- (docs/30, docs/90).
      local native = button.on_draw
      local sunk = 0
      button.on_draw = function(self)
        local want = self.pressed and 3 or 0
        if want ~= sunk then
          sunk = want
          label:place(10 + want, 10 + want)
        end
        native(self)
      end
    end
  end
end

-- **A Quit control, because a page with controls does not close on three taps of the centre key**: the dial
-- walks onto it and the centre presses it, and it is the way out this page offers on a board whose middle key
-- is Enter (docs/30, docs/90).
local leaving = false
local quit = ui.button(130, 80)
if quit ~= nil then
  quit.background = 0
  quit.border = 15
  quit:place(390, 800)
  local title = ui.label(110, 60)
  title.text = 'Quit'
  title.color = 15
  title.background = 0
  title.align = 'center'
  title.valign = 'middle'
  title:place(10, 10)
  quit:add(title)
  quit.on_click = function(self)
    leaving = true
    console.print('buttons: quit pressed, leaving the page')
  end
end

ui.rect(0, 780, view.width, 120, 0, 'gc16')
ui.text(20, 796, '8 BUTTONS, 8 LABELS, 0 PIXELS OF THEIR OWN', 2, 15)
ui.text(20, 824, 'a button is a frame; the label is its child', 1, 11)
ui.text(20, 842, 'the child is clipped to the frame and drawn after it', 1, 8)
ui.frame(4, 4, view.width - 8, view.height - 8, 0, 'gc16')

-- **The first page is the one that is worth a wash**: `gc16` cleans the glass once and leaves no ghost of the
-- page before it. From here on a press is a `du`, which is the quick paint, because a finger that waits for a
-- flash twice a press (down and up) is a button that does not feel like a button (docs/50, docs/90).
ui.present('gc16')
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: %d buttons, %d labels, %d button draws, %d label draws after the page',
  #buttons, #labels, button_draws, label_draws))

-- Nothing changed: neither a button nor a label is drawn.
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: %d button draws, %d label draws after a present with nothing new',
  button_draws, label_draws))

-- A label that changes draws only itself.
labels[1].text = 'SYSTEM / live'
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: %d button draws, %d label draws after one label changed',
  button_draws, label_draws))
labels[1].text = 'SYSTEM / 8'
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: %d button draws, %d label draws after putting it back',
  button_draws, label_draws))

-- The pressed frame is a button change: the button redraws and asks its child to draw again, because the
-- fill of the frame has covered the label.
buttons[8].pressed = false
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: %d button draws, %d label draws after releasing the button',
  button_draws, label_draws))
buttons[8].pressed = true
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: %d button draws, %d label draws after pressing it again',
  button_draws, label_draws))

-- **A click the way the dial does it**: the system puts the control down, calls the hook and lifts it. The
-- app can call the same hook by hand, which is how this page is tried without a finger; what has to be true
-- is that **a click is one label that changes** and nothing else.
local before_button, before_label = button_draws, label_draws
buttons[1].on_click(buttons[1])
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: after a click, %d button draws (%+d) and %d label draws (%+d)',
  button_draws, button_draws - before_button, label_draws, label_draws - before_label))

-- And another, which is the counter of the button and not of the page.
buttons[1].on_click(buttons[1])
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('buttons: after a second click, %d button draws, %d label draws',
  button_draws, label_draws))

console.print(string.format('buttons: %d presses and %d focus moves; %s, %s', clicks, focuses,
  tostring(buttons[1]), tostring(labels[1])))

-- **The app stays and listens, and that is the whole point of a callback**: an app that ends after drawing is
-- an app nobody can press, which is what this example did until it was caught. `ui.present` with nothing new
-- draws nothing —the counts above are the proof— so the loop presents every turn and the glass is painted
-- only when a label or a ring has really moved (docs/30, docs/90).
while not leaving do
  ui.present()
  sys.delay(200):await()
end
console.print('buttons: out')
if face ~= nil then face:release() end
