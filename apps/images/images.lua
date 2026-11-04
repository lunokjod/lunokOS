-- images: **one picture of every family the system reads**, and what every family can do.
--   run images
--
-- The pictures are in `/res` —what the app carries—: a photograph in JPEG, a poster in 24-bit BMP, eight inks in an
-- 8-bit BMP, a drawing with transparency in GIF, an icon with alpha in ICO, thin lines in PNG —which it does not
-- sweat over—, two gradients in PGM and PPM and a one-bit picture in PBM. **They are not the same drawing nine
-- times**: every format is seen for what it can do, and the line under it says so.
--
-- `ui.image_info` reads the header and says the family and the size; `ui.image` decodes the file into the canvas row
-- by row, and the page only decides where it lands and how big. The grey box behind is so that **the transparency
-- is seen** of the GIF and of the ICO: what is behind is the paper, and over grey it stands out.
local caps = sys.caps()
if not caps.display then
  console.print('images: this board has no glass')
  return
end

local view = ui.caps()
ui.title('Images')

-- **Every file with what it does there**: the name, and the sentence that explains why this format and not another.
local pictures = {
  { path = '/res/photo.jpg', why = 'JPEG: a photograph, and the gradients hold' },
  { path = '/res/poster.bmp', why = 'BMP 24 bits: flat ink, not a byte wasted' },
  { path = '/res/logo8.bmp', why = 'BMP 8 bits: eight inks and a table' },
  { path = '/res/cartoon.gif', why = 'GIF: a palette and a transparent corner' },
  { path = '/res/icon.ico', why = 'ICO: an icon, alpha and all' },
  { path = '/res/lines.png', why = 'PNG: thin lines, exactly as drawn' },
  { path = '/res/shades.pgm', why = 'PGM: grey, eight bits, one ink' },
  { path = '/res/colours.ppm', why = 'PPM: colour, eight bits a channel' },
  { path = '/res/checks.pbm', why = 'PBM: one bit, ink or paper' },
}

local index = 1
local shown = 0
local failures = 0
local leaving = false
local controls = {}
-- **A change of focus is a change of state of the page and has to be washed**: the ring is black over white and a
-- `du` leaves the one from before at a quarter of the ink, which is a grey ring nobody asked for. The turn after the
-- movement paints it with `gc16`, and since the buttons are the only thing that changed, the region that flashes is
-- the row of buttons and not the page (docs/30, docs/50).
local clean = false

local top = 64
local box_w = view.width - 80
local box_h = view.height - 340

local function info_of(path)
  local info, err = ui.image_info(path)
  if info == nil then
    console.print(string.format('images: %s: %s', path, tostring(err and err.message)))
  end
  return info
end

local function bytes_of(path)
  local stat = fs.stat(path)
  return (stat ~= nil and stat.size) or 0
end

local function draw(i)
  local picture = pictures[i]
  local info = info_of(picture.path)
  ui.clear(15)
  -- **The buttons come back to the page**: `ui.clear` wipes the view and every button is its own canvas, so what has
  -- not changed would not be composed again and the page would lose it (docs/30, docs/90).
  for _, control in ipairs(controls) do control:invalidate() end
  ui.text(40, 12, string.format('%d / %d', i, #pictures), 2, 0)
  if info == nil then
    failures = failures + 1
    ui.text(40, top, 'no picture', 2, 0)
    ui.text(40, top + 60, picture.path, 1, 8)
    ui.present('gc16')
    return
  end
  -- **The picture into the box with its proportion**: the scale is the smaller of the two and the size it is drawn at
  -- is the one of the file times that scale (docs/30).
  local scale = math.min(box_w / info.width, box_h / info.height)
  local w = math.max(1, math.floor(info.width * scale))
  local h = math.max(1, math.floor(info.height * scale))
  local x = math.floor((view.width - w) / 2)
  local y = top + math.floor((box_h - h) / 2)
  -- **A grey background the size of the drawing**, so that the transparency is seen: what is not of the GIF is the
  -- paper and over grey it stands out what is there and what is not (docs/30).
  ui.rect(x, y, w, h, 11)

  local ok, why = ui.image(x, y, picture.path, w, h)
  if ok == nil then
    failures = failures + 1
    console.print(string.format('images: %s: %s', picture.path, tostring(why and why.message)))
    ui.text(40, top, 'not drawn', 2, 0)
    ui.text(40, top + 60, tostring(why and why.message), 1, 8)
  else
    shown = shown + 1
    console.print(string.format('images: %s %dx%d (%d bytes) at %dx%d, %d of %d', info.format, info.width,
      info.height, bytes_of(picture.path), w, h, i, #pictures))
  end
  ui.text(40, view.height - 236, string.format('%s  %d x %d  %d B', info.format, info.width, info.height,
    bytes_of(picture.path)), 2, 0)
  ui.text(40, view.height - 196, picture.path, 1, 8)
  ui.text(40, view.height - 168, picture.why, 1, 0)
  ui.present('gc16')
  -- The whole page has just come out of a `gc16`: there is no ring pending a wash.
  clean = false
end

local function step(delta)
  index = index + delta
  if index < 1 then index = #pictures end
  if index > #pictures then index = 1 end
  draw(index)
end

-- **Three buttons at the foot of the page**: back, quit and next, which with the dial of the board is the same as
-- with a finger (docs/30, docs/90).
local function make_button(x, width, text, action)
  local button, err = ui.button(width, 72)
  if button == nil then
    console.print(string.format('images: no room for "%s": %s', text, tostring(err and err.message)))
    return nil
  end
  button.background = 15
  button.border = 0
  button:place(x, view.height - 84)
  local label, label_err = ui.label(width - 20, 52)
  if label == nil then
    console.print(string.format('images: no room for the label: %s',
      tostring(label_err and label_err.message)))
    return nil
  end
  label.text = text
  label.align = 'center'
  label.valign = 'middle'
  label.scale = 2
  label:place(10, 10)
  button:add(label)
  button.on_click = action
  button.on_activate = action
  button.on_focus = function(self, focused)
    if focused then clean = true end
  end
  controls[#controls + 1] = button
  return button
end

make_button(30, 150, 'back', function() step(-1) end)
make_button(205, 140, 'quit', function()
  leaving = true
  console.print('images: quit pressed, leaving the page')
end)
make_button(view.width - 170, 140, 'next', function() step(1) end)

draw(index)
console.print(string.format('images: %d pictures, %d drawn, %d failed', #pictures, shown, failures))

-- **The app stays and listens**: an app that folds after painting is an app nobody can press. The picture of every
-- step is presented with its wash inside `draw`; the turns in between present only what the controls have changed
-- —nothing, almost always— and cost no ink (docs/30, docs/90).
while not leaving do
  -- **The turn after a movement of focus is a wash**: `gc16` is what leaves the ring that arrives sharp and the one
  -- that goes erased, and only the buttons are in the damage list, so what flashes is the row of buttons
  -- (docs/30, docs/90).
  ui.present(clean and 'gc16' or nil)
  clean = false
  sys.delay(200):await()
end
console.print('images: out')
