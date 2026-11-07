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
--
-- **The page is made of controls and not of places** (0.118.0): the nine pictures are a `ui.icon_grid` that the dial
-- walks and the centre picks, the numbers and the sentence of the format are a **box that is read and not written**
-- (`readonly`, 0.116.0) which the centre enters so that the arrows walk its rows, and where the page is says a
-- `ui.dots`. What is left for the app itself is the drawing of the picture in the middle and the grey plate under
-- it, which is not a control anybody can press.
--
-- **A page of pictures is where a stack overflow was found** (0.118.0, the owner's board: `***ERROR*** A stack
-- overflow in task loopTask has been detected` after three pictures). The loader did use the PSRAM for the file and
-- for the block —`pal::extra_memory()`—, but the decoders kept their own tables in locals: the table of a GIF is
-- 12 288 bytes, its string 4 096 and its row 2 048, against the 7 320 the app task prints free. The state of every
-- decoder is in the caller's block now, and a page of nine pictures is nine small frames (docs/75, docs/85, §33).
local caps = sys.caps()
if not caps.display then
  console.print('images: this board has no glass')
  return
end

local view = ui.caps()
ui.title('Images')

-- **Every file with what it does there**: the name, and the sentence that explains why this format and not another.
local pictures = {
  { name = 'photo.jpg', path = '/res/photo.jpg', why = 'JPEG: a photograph, and the gradients hold' },
  { name = 'poster.bmp', path = '/res/poster.bmp', why = 'BMP 24 bits: flat ink, not a byte wasted' },
  { name = 'logo8.bmp', path = '/res/logo8.bmp', why = 'BMP 8 bits: eight inks and a table' },
  { name = 'cartoon.gif', path = '/res/cartoon.gif', why = 'GIF: a palette and a transparent corner' },
  { name = 'icon.ico', path = '/res/icon.ico', why = 'ICO: an icon, alpha and all' },
  { name = 'lines.png', path = '/res/lines.png', why = 'PNG: thin lines, exactly as drawn' },
  { name = 'shades.pgm', path = '/res/shades.pgm', why = 'PGM: grey, eight bits, one ink' },
  { name = 'colours.ppm', path = '/res/colours.ppm', why = 'PPM: colour, eight bits a channel' },
  { name = 'checks.pbm', path = '/res/checks.pbm', why = 'PBM: one bit, ink or paper' },
}

local index = 1
local shown = 0
local failures = 0
local leaving = false
local controls = {}
-- **A change of focus is a change of state of the page and has to be washed**: the ring is black over white and a
-- `du` leaves the one from before at a quarter of the ink, which is a grey ring nobody asked for. The turn after the
-- movement paints it with `gc16`, and since the controls are the only thing that changed, what flashes is the strip
-- and the rows and not the picture (docs/30, docs/50).
local clean = false

local gap = 10
-- **Where the page is says a strip of dots and not a text**: the ball of the page that is read is twenty-five pixels
-- and the strip leaves twelve of air under itself, which is the counter of the paper project (0.112.0, docs/92).
local dots = ui.dots(view.width - 2 * 12, 37)
dots:place(12, 8)
dots.pages = #pictures
dots.current = index
controls[#controls + 1] = dots

-- **The sentence of the format is a box that is read**, and a person enters it with the centre to walk the rows of a
-- long one: the caret is there and the keyboard never rises (0.116.0). It has no frame, so the paper of the page
-- reaches its edge and what is on the glass is a paragraph and not a field (docs/30, docs/92).
local notes_h = 120
local notes = ui.textbox_multiline(view.width - 2 * 12, notes_h)
notes.scale = 2
notes.wrap = true
notes.readonly = true
notes.framed = false
notes.cursor = 0
notes.top = 0
notes.text = ''
controls[#controls + 1] = notes

-- **The nine families are a grid of cells**, which is the control that picks one of nine things on this glass: the
-- arrows walk the cells, the ring says where the dial is, the chosen cell is inverted, and the centre picks it with
-- `on_pick`. A pair of buttons called "back" and "next" walked that menu one step at a time and could not say which
-- of the nine was on the glass (docs/30, docs/90).
--
-- Two rows of five cells: the mark of the cell is the one or two letters of the family and the name under it is the
-- extension of the file, which is what a person reading the list of `/res` sees. A cell costs `icon + 8 + line + 8`
-- —ninety pixels of the mark, thirty-three of line at scale 3— and nine cells in two rows of five is one page, so
-- the control draws no footer and needs no page turning (docs/92).
local cells = {
  { label = 'jpg', mark = 'J' },
  { label = 'bmp', mark = 'B' },
  { label = 'bmp8', mark = '8' },
  { label = 'gif', mark = 'G' },
  { label = 'ico', mark = 'I' },
  { label = 'png', mark = 'P' },
  { label = 'pgm', mark = 'g' },
  { label = 'ppm', mark = 'c' },
  { label = 'pbm', mark = 'b' },
}
-- **`draw` is declared here and given its body below**, because the grid's hook is written before the function it calls
-- and a `local function draw()` further down is not in scope up here: the hook would keep a nil global called `draw` and
-- the picture would not change when a cell is picked (found by the test that presses the dial, 0.118.0).
local draw

local grid_h = 250
local grid = ui.icon_grid(view.width - 2 * 12, grid_h)
grid.columns = 5
-- **The dense text of the paper and not the body**: a cell is a hundred and three pixels wide and the name under the
-- mark is the extension of the file, which is four letters —`bmp8` at scale 3 was cut to `bm...` and a name cut with
-- three dots is not the name of a format (found on the bench with this page, 0.118.0, docs/92).
grid.scale = 2
grid.items = cells
grid.index = index
grid.marked = index
grid.on_pick = function(self, picked)
  if picked >= 1 and picked <= #pictures then
    index = picked
    draw()
  end
end
controls[#controls + 1] = grid

-- **The button that leaves the page**, which is the one control the paper project gives every page (docs/90).
local button_h = 72
local quit = ui.button(view.width - 2 * 12, button_h)
local quit_label = ui.label(view.width - 2 * 12 - 20, 52)
quit_label.text = 'quit'
quit_label.align = 'center'
quit_label.valign = 'middle'
quit_label:place(10, 10)
quit:add(quit_label)
quit.on_click = function()
  leaving = true
  console.print('images: quit pressed, leaving the page')
end
quit.on_activate = quit.on_click
quit.on_focus = function(self, focused)
  if focused then clean = true end
end
controls[#controls + 1] = quit

-- **The places of the page, measured once**: what the controls do not take is the picture's, which is the only thing
-- the app sets by hand. The three at the foot have their heights and the picture takes the rest, so a board with a
-- taller glass draws a bigger picture and nothing moves (docs/30).
local picture_y = 8 + 37 + gap
local picture_h = view.height - picture_y - (3 * gap) - notes_h - grid_h - button_h - 12
notes:place(12, picture_y + picture_h + gap)
grid:place(12, picture_y + picture_h + gap + notes_h + gap)
quit:place(12, view.height - button_h - 12)

-- **The dial starts on the pictures and not on the paragraph**: the page is a menu of nine, so the ring of the first cell
-- is on the glass from the first wash and one press of a key walks the families. Without this the focus would be the
-- first control that was made —the strip of dots is not a stop, so the paragraph— and the arrows there would walk its
-- rows, which is a page that answers the wrong question. It is asked **after the three places**, because `ui.focus`
-- walks the controls of the page and a control that is not on it yet is not one of them (0.118.0, docs/30).
ui.focus(grid)

local function bytes_of(path)
  local stat = fs.stat(path)
  return (stat ~= nil and stat.size) or 0
end

-- **The picture into its box with its proportion**: the scale is the smaller of the two and the size it is drawn at
-- is the one of the file times that scale (docs/30).
function draw()
  local picture = pictures[index]
  ui.clear(15)
  -- **The controls come back to the page**: `ui.clear` wipes the view and every control is its own canvas, so what has
  -- not changed would not be composed again and the page would lose it (docs/30, docs/90).
  for _, control in ipairs(controls) do control:invalidate() end
  dots.current = index
  grid.index = index
  grid.marked = index
  local info = ui.image_info(picture.path)
  if info == nil then
    failures = failures + 1
    notes.text = string.format('%s\nnot a picture this board reads', picture.path)
    ui.text(40, picture_y + 40, 'no picture', 2, 0)
    ui.present('gc16')
    clean = false
    return
  end
  local box_w = view.width - 2 * 24
  local scale = math.min(box_w / info.width, picture_h / info.height)
  local w = math.max(1, math.floor(info.width * scale))
  local h = math.max(1, math.floor(info.height * scale))
  local x = math.floor((view.width - w) / 2)
  local y = picture_y + math.floor((picture_h - h) / 2)
  -- **A grey background the size of the drawing**, so that the transparency is seen: what is not of the GIF is the
  -- paper and over grey it stands out what is there and what is not (docs/30).
  ui.rect(x, y, w, h, 11)

  local bytes = bytes_of(picture.path)
  local ok, why = ui.image(x, y, picture.path, w, h)
  if ok == nil then
    failures = failures + 1
    console.print(string.format('images: %s: %s', picture.path, tostring(why and why.message)))
    notes.text = string.format('%s\nnot drawn: %s', picture.path, tostring(why and why.message))
    ui.text(40, picture_y + 40, 'not drawn', 2, 0)
  else
    shown = shown + 1
    console.print(string.format('images: %s %dx%d (%d bytes) at %dx%d, %d of %d', info.format, info.width,
      info.height, bytes, w, h, index, #pictures))
    -- **The numbers and the sentence in the box that is read**, and the box walks its rows when the centre enters it:
    -- a format that needs two lines is a format this page still explains whole (0.116.0).
    notes.text = string.format('%s  %s  %d x %d  %d B\n%s', picture.name, info.format, info.width, info.height,
      bytes, picture.why)
  end
  notes.top = 0
  ui.present('gc16')
  -- The whole page has just come out of a `gc16`: there is no ring pending a wash.
  clean = false
end

draw()
console.print(string.format('images: %d pictures, %d drawn, %d failed', #pictures, shown, failures))

-- **The app stays and listens**: an app that folds after painting is an app nobody can press. The picture of every
-- step is presented with its wash inside `draw`; the turns in between present only what the controls have changed
-- —nothing, almost always— and cost no ink (docs/30, docs/90).
while not leaving do
  -- **The turn after a movement of focus is a wash**: `gc16` is what leaves the ring that arrives sharp and the one
  -- that goes erased, and only the controls are in the damage list, so what flashes is the strip, the rows and the
  -- button and not the picture (docs/30, docs/90).
  ui.present(clean and 'gc16' or nil)
  clean = false
  sys.delay(200):await()
end
console.print('images: out')
