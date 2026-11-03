-- Light: a colour wheel on the board's LED, and the button sets how bright it is
-- (docs/30, docs/50).
--   run light
--
-- One press goes up a step and, from the top, back to the bottom; a long press goes
-- straight to the bottom. The colour never stops moving: the hue walks in a loop
-- and each frame is one colour and one small wait, so the kernel keeps turning and
-- the button is read while it shines.
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
local hue = 0

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

local function show()
  local r, g, b = wheel(hue, steps[step])
  led.set(math.floor(r + 0.5), math.floor(g + 0.5), math.floor(b + 0.5))
end

-- The button, called between two frames with the coroutine suspended, so this
-- cannot wait and only changes where the light is going.
function on_button(kind)
  if kind == 'long' then
    step = 1
  else
    step = step + 1
    if step > #steps then step = 1 end
  end
  console.print('brightness ' .. steps[step] .. ' of ' .. top)
  show()
end

local caps = led.caps()
if caps.count == 0 then
  console.print('this board has no light')
  return
end
top = caps.max
for i = 1, 9 do
  steps[i] = math.max(1, math.floor(top * i / 9 + 0.5))
end

console.print('a colour wheel; press for brightness (step 1 of 9 is ' .. steps[1] ..
                ' of ' .. top .. ')')
while true do
  show()
  hue = (hue + 0.006) % 1
  sys.delay(30):await()
end
