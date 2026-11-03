-- label: a page of labels — four faces, four sizes, the two alignments, cuts and the sixteen levels.
--   run label
--
-- **A label is a control without a buffer**: its frame is the background it paints and the border that cuts
-- the line, and it costs no pixels of its own. The face is the one `cv:font` gives it, the colour is the
-- text level and the background its own; the line is placed by `align` and `valign`, and when it does not
-- fit the label ends it with `...` (docs/30, docs/90).
--
-- Three faces are loaded at once, which is the most the system keeps. The fourth face of the page is the
-- built-in 5x7, so it costs no slot: `label:font(nil)` is the system face, not a fallback for a missing
-- file.
--
-- The counts of the console are the other half of the control: a present with nothing new does not call
-- `on_draw`, and a field with the same value is not a change.
local caps = sys.caps()
if not caps.display then
  console.print('label: this board has no glass')
  return
end

local view = ui.caps()
console.print(string.format('label: view %d x %d at %d,%d', view.width, view.height, view.x, view.y))
ui.title('Label')

local faces = {}
local function load(name, path)
  local face, err = ui.load_font(path)
  if face == nil then
    console.print(string.format('label: no %s face: %s %s', name,
      tostring(err and err.code), tostring(err and err.message)))
    return nil
  end
  local width, height, glyphs = face:size()
  console.print(string.format('label: %s %dx%d, %d glyphs, Ag=%d px at 2', name, width, height,
    glyphs, face:text_width('Ag', 2)))
  return face
end

faces.sans = load('sans', '/fonts/dejavusans.ttf')
faces.serif = load('serif', '/fonts/dejavuserif.ttf')
faces.mono = load('mono', '/fonts/dejavusansmono.ttf')

local draws = 0
local labels = 0
local function make(spec)
  local label = ui.label(spec.w, spec.h)
  if label == nil then
    console.print(string.format('label: no room for "%s"', spec.text))
    return nil
  end
  labels = labels + 1
  label.text = spec.text
  label.color = spec.color or 0
  label.background = spec.background or 15
  label.align = spec.align or 'left'
  label.valign = spec.valign or 'middle'
  label.scale = spec.scale or 1
  if label:font(spec.face) == nil then
    console.print(string.format('label: "%s" has no face', spec.text))
  end
  label:place(spec.x, spec.y)
  local native = label.on_draw
  label.on_draw = function(self)
    draws = draws + 1
    native(self)
  end
  return label
end

local function heading(y, text)
  ui.text(20, y, text, 2, 0)
end

ui.clear(15, 'gc16')
local W = view.width
local title = make{x = 20, y = 10, w = W - 40, h = 72, text = 'UILabel', scale = 5,
                   face = faces.serif, color = 0, background = 15, align = 'center', valign = 'middle'}
if title == nil then return end
ui.font(faces.sans)
ui.text(20, 88, 'four faces / four sizes / sixteen levels', 1, 8)
ui.font(nil)
ui.rect(20, 100, W - 40, 2, 0, 'gc16')

-- Four faces: the three loaded files and the 5x7 of the system. The same pair of letters in each one.
ui.rect(0, 104, W, 136, 13, 'gc16')
heading(108, 'FACES')
local column_x = {10, 143, 276, 409}
local column_w = 121
local family = {
  {text = 'Sans Ag', face = faces.sans},
  {text = 'Serif Ag', face = faces.serif},
  {text = 'Mono Ag', face = faces.mono},
  {text = 'SYSTEM', face = nil},
}
for i = 1, #family do
  make{x = column_x[i], y = 132, w = column_w, h = 100, text = family[i].text,
       face = family[i].face, scale = 2, color = 0, background = 15,
       align = 'center', valign = 'middle'}
end

-- The body of the same face at the four sizes, which is the scale in eighths of a pixel.
ui.rect(0, 246, W, 136, 15, 'gc16')
heading(250, 'SIZES 1-4')
for i = 1, 4 do
  make{x = column_x[i], y = 274, w = column_w, h = 100, text = string.format('%d Ag', i),
       face = faces.sans, scale = i, color = 0, background = 15,
       align = 'center', valign = 'middle'}
end

-- Both alignments in one row: the label is the same size, only the line inside it moves.
ui.rect(0, 388, W, 136, 13, 'gc16')
heading(392, 'ALIGN + VALIGN')
local align_x = {10, 190, 370}
local align_w = 160
local align = {
  {text = 'left', align = 'left', valign = 'top', background = 15, color = 0},
  {text = 'centre', align = 'center', valign = 'middle', background = 8, color = 15},
  {text = 'right', align = 'right', valign = 'bottom', background = 0, color = 15},
}
for i = 1, #align do
  make{x = align_x[i], y = 416, w = align_w, h = 100, text = align[i].text,
       face = faces.sans, scale = 2, color = align[i].color, background = align[i].background,
       align = align[i].align, valign = align[i].valign}
end

-- The same sentence in two faces and two colours, cut by the frame when it no longer fits.
ui.rect(0, 530, W, 120, 15, 'gc16')
heading(534, 'CUTS WITH ...')
local cut_text = 'The quick brown fox jumps over the lazy dog'
local serif_cut = make{x = 10, y = 558, w = 250, h = 84, text = cut_text, face = faces.serif,
                       scale = 2, color = 0, background = 15, align = 'left', valign = 'middle'}
local mono_cut = make{x = 280, y = 558, w = 250, h = 84, text = cut_text .. ' again',
                      face = faces.mono, scale = 2, color = 15, background = 0,
                      align = 'right', valign = 'middle'}

-- All sixteen levels: the background is the level and the text is the other end of the scale.
ui.rect(0, 656, W, 152, 13, 'gc16')
heading(660, '16 LEVELS')
for i = 0, 15 do
  local row = (i < 8) and 0 or 1
  local column = i % 8
  make{x = 10 + column * 65, y = 684 + row * 52, w = 60, h = 44, text = tostring(i),
       face = faces.mono, scale = 2, color = (i < 8) and 15 or 0, background = i,
       align = 'center', valign = 'middle'}
end

-- The footer is direct drawing, not a label: it shows where the controls stop and the page continues.
ui.rect(0, 816, W, 108, 0, 'gc16')
ui.font(faces.sans)
ui.text(20, 830, '30 LABELS, 0 PIXELS OF THEIR OWN', 2, 15)
ui.text(20, 858, 'each one is a frame and one line; gc16 washes the old word', 1, 11)
ui.text(20, 876, 'the system keeps three loaded faces; the fourth is its own 5x7', 1, 8)
ui.font(nil)
ui.frame(4, 4, W - 8, view.height - 8, 0, 'gc16')

ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('label: %d labels, %d draws after the page', labels, draws))

-- The same page again: no on_draw, no paint. This is the whole point of the control.
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('label: %d draws after a present with nothing new', draws))

-- A text and a colour are two reasons to draw the same label, and one draw is enough.
title.text = 'UILabel!'
title.color = 15
title.background = 0
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('label: %d draws after a text and a colour', draws))

-- Putting it back is another change; asking for the same words again is not.
title.text = 'UILabel'
title.color = 0
title.background = 15
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('label: %d draws after putting it back', draws))
title.text = 'UILabel'
ui.present()
while not ui.idle() do sys.delay(20):await() end
console.print(string.format('label: %d draws after the same words again', draws))

console.print(string.format('label: %s, %s', tostring(serif_cut), tostring(mono_cut)))
for _, key in ipairs({'sans', 'serif', 'mono'}) do
  if faces[key] ~= nil then faces[key]:release() end
end
