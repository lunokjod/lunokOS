-- buttons: eight UIButtons, each with a UILabel inside.
--   run buttons
--
-- **A button is the first control that is a container with a frame**: it paints a background and a border,
-- and a child label is added with `button:add(label)` and placed in the button's coordinates. The button
-- clips the child to its frame; the child is composed after the button, so the frame is behind the text
-- (docs/30, docs/90).
--
-- The three faces loaded at once are the most the system keeps; the fourth face is its own 5x7, which
-- costs no slot. The labels use the four sizes (scale 1 to 4) and the levels of the panel; the buttons
-- themselves use four backgrounds and borders, and the last one is painted pressed.
--
-- The counts of the console are the other half: a present with nothing new draws neither a button nor a
-- label, a label that changes draws only itself, and a button that changes draws its frame and asks its
-- child to draw again because the opaque frame covered it.
local caps = sys.caps()
if not caps.display then
  console.print('buttons: this board has no glass')
  return
end

local view = ui.caps()
console.print(string.format('buttons: view %d x %d at %d,%d', view.width, view.height, view.x, view.y))
ui.title('Buttons')

local faces = {}
local function load(name, path)
  local face, err = ui.load_font(path)
  if face == nil then
    console.print(string.format('buttons: no %s face: %s %s', name,
      tostring(err and err.code), tostring(err and err.message)))
    return nil
  end
  local width, height, glyphs = face:size()
  console.print(string.format('buttons: %s %dx%d, %d glyphs, Ag=%d px at 2', name, width, height,
    glyphs, face:text_width('Ag', 2)))
  return face
end

faces.sans = load('sans', '/res/dejavusans.ttf')
faces.serif = load('serif', '/res/dejavuserif.ttf')
faces.mono = load('mono', '/res/dejavusansmono.ttf')

local button_draws = 0
local label_draws = 0
local buttons = {}
local labels = {}

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

-- The four faces, the four sizes and the levels: a button background is also the label's background, so
-- the fill the label writes is the one the button has under it and the border remains visible around it.
local specs = {
  {x = 10, y = 90, w = 250, h = 150, background = 15, border = 0,
   text = 'SYSTEM / 8', face = nil, scale = 1, color = 0},
  {x = 280, y = 90, w = 250, h = 150, background = 12, border = 0,
   text = 'Serif / 16', face = faces.serif, scale = 2, color = 0},
  {x = 10, y = 260, w = 250, h = 150, background = 8, border = 15,
   text = 'Mono / 24', face = faces.mono, scale = 3, color = 15},
  {x = 280, y = 260, w = 250, h = 150, background = 0, border = 15,
   text = 'SYSTEM / 16', face = nil, scale = 2, color = 15},
  {x = 10, y = 430, w = 250, h = 150, background = 15, border = 8,
   text = 'Sans / 16', face = faces.sans, scale = 2, color = 0},
  {x = 280, y = 430, w = 250, h = 150, background = 0, border = 15,
   text = 'Serif / 24', face = faces.serif, scale = 3, color = 15},
  {x = 10, y = 600, w = 250, h = 150, background = 15, border = 0,
   text = 'Mono / 32', face = faces.mono, scale = 4, color = 0},
  {x = 280, y = 600, w = 250, h = 150, background = 8, border = 15,
   text = 'Sans / 24', face = faces.sans, scale = 3, color = 15, pressed = true},
}

ui.clear(15, 'gc16')
ui.text(20, 12, 'UIBUTTON', 4, 0)
ui.text(20, 52, 'A FRAME THAT HOLDS A LABEL', 1, 8)
ui.rect(20, 74, view.width - 40, 2, 0, 'gc16')
ui.rect(0, 84, view.width, 686, 13, 'gc16')

for i = 1, #specs do
  local spec = specs[i]
  local button = make_button(spec)
  if button ~= nil then
    add_label(button, {w = 230, h = 130, text = spec.text, background = spec.background,
                       color = spec.color, scale = spec.scale, face = spec.face})
  end
end

ui.rect(0, 780, view.width, 120, 0, 'gc16')
ui.text(20, 796, '8 BUTTONS, 8 LABELS, 0 PIXELS OF THEIR OWN', 2, 15)
ui.text(20, 824, 'a button is a frame; the label is its child', 1, 11)
ui.text(20, 842, 'the child is clipped to the frame and drawn after it', 1, 8)
ui.frame(4, 4, view.width - 8, view.height - 8, 0, 'gc16')

ui.present()
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

console.print(string.format('buttons: %s, %s', tostring(buttons[1]), tostring(labels[1])))
for _, key in ipairs({'sans', 'serif', 'mono'}) do
  if faces[key] ~= nil then faces[key]:release() end
end
