-- Matrix: colour ramps that move across the twenty-five lights of the panel, and
-- the button sets how bright they are (docs/30, docs/50).
--   run matrix
--
-- This is the app of the M5 ATOM Matrix: what `light` says with one colour, this
-- one says with a picture. A frame is twenty-five `led.pixel` calls with the same
-- brightness and a hue that changes from light to light, so the panel is a ramp of
-- colour; the hue also changes from frame to frame, so the ramp walks.
--
-- Three ramps, and the ramp is what moves:
--   1. two slopes, so the bands are diagonal and they travel one way;
--   2. a wave, so the bands bend and run across the panel;
--   3. two sines that do not share a period, so no two frames are the same.
-- A press goes up a brightness step and, from the top, back to the bottom; a long
-- press is the next ramp, and the brightness stays where it was.
--
-- Out with three taps or three ESC, which is the system's gesture and not the
-- app's (docs/30). A press that steps the brightness also counts for those three,
-- so two quick steps and a third press close the app: that is the price of one
-- button, and it is written here so it does not surprise anybody.

-- What this light can take, said by the board: 255 on the Lite, 20 on the panel of
-- the Matrix, where more heats the LEDs and the case (docs/30, docs/50). The steps
-- are fractions of that ceiling, so the top step is the top the hardware has.
local top = 0
local steps = {}
local step = 1
local effect = 1
local t = 0

-- A cheap hue-to-rgb, saturation fixed: it is a light and not a colourimeter.
local function wheel(h, v)
  local i = math.floor(h * 6)
  local f = h * 6 - i
  local p = 0
  local q = v * (1 - f)
  local t = v * f
  local sector = i % 6
  if sector == 0 then return v, t, p end
  if sector == 1 then return q, v, p end
  if sector == 2 then return p, v, t end
  if sector == 3 then return p, q, v end
  if sector == 4 then return t, p, v end
  return v, p, q
end

-- One hue per light and per frame, from the row, the column and the clock. The
-- brightness of a frame is the same for the whole panel on purpose: what has to
-- move is the colour, and a brightness that goes up and down every frame is a
-- flicker and not an animation.
local ramps = {
  function(row, col, now) return now + row * 0.10 + col * 0.22 end,
  function(row, col, now)
    return now * 0.45 + 0.30 * math.sin(row * 0.7 + col * 0.7 + now * 2.0)
  end,
  function(row, col, now)
    return now * 0.70 + 0.18 * math.sin(row * 0.9 + now) +
             0.18 * math.sin(col * 0.9 - now * 0.8)
  end,
}

local caps = led.caps()
local lights = caps.count
if lights == 0 then
  console.print('this board has no light')
  return
end

-- The panel is twenty-five lights, 5 by 5. A board with fewer gets the ramp it can
-- show and it is said, so nobody has to wonder why most of the picture is missing.
local panel = lights
top = caps.max
for i = 1, 9 do
  steps[i] = math.max(1, math.floor(top * i / 9 + 0.5))
end
if lights < 25 then
  console.print('this board has ' .. lights .. ' lights; the panel of the Matrix has 25')
end

-- One frame: every light of the panel gets its hue, and the app owns the whole
-- light while it lives. Giving it back is the system's job (docs/30).
local function paint()
  local ramp = ramps[effect]
  local value = steps[step]
  for i = 0, panel - 1 do
    local row = math.floor(i / 5)
    local col = i % 5
    local r, g, b = wheel(ramp(row, col, t) % 1, value)
    led.pixel(i, math.floor(r + 0.5), math.floor(g + 0.5), math.floor(b + 0.5))
  end
end

-- The button, called between two frames with the coroutine suspended, so this
-- cannot wait: it only changes where the ramp is going.
function on_button(kind)
  if kind == 'long' then
    effect = effect % #ramps + 1
    console.print('ramp ' .. effect .. ' of ' .. #ramps)
  else
    step = step + 1
    if step > #steps then step = 1 end
    console.print('brightness ' .. steps[step] .. ' of ' .. top)
  end
  paint()
end

console.print('a colour wheel around ' .. panel .. ' lights; press for brightness (step 1 of 9 is ' ..
                steps[1] .. ' of ' .. top .. '), hold for the next ramp')
while true do
  paint()
  -- The clock of the animation: about two seconds for a hue to cross the panel,
  -- which is fast enough to be movement and slow enough to see the band.
  t = t + 0.015
  sys.delay(30):await()
end
