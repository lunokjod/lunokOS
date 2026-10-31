-- Tilt: the twenty-five lights of the panel as a spirit level (docs/30, docs/50).
--   run tilt
--
-- The sensor says which way gravity pulls, and the bubble is one light: the middle
-- is level and the edges are the board stood on its side. The console says the
-- numbers behind the light, because a level nobody can check is worth nothing and
-- the way to check it is to see what it read.
--
-- The sensor is a chip on a bus and it may not answer: then the app says so and
-- leaves, instead of freezing on a level that lies (docs/30).
--
-- Out with three taps or three ESC, which is the system's gesture and not the
-- app's (docs/30).

local caps = led.caps()
if caps.count ~= 25 then
  console.print('tilt draws on the twenty-five lights of the Matrix, and this ' ..
                  'board has ' .. caps.count)
  return
end

-- Half of what the panel takes: a bubble that is comfortable to look at, and the
-- ceiling is the board's anyway (docs/30, docs/50).
local top = caps.max
local bubble = math.max(1, math.floor(top / 2))

-- Where one axis falls on the panel, from 0 to 4: -1 g is one edge and +1 g the
-- other. Out of range is clamped, because a board that gets shaken reads more than
-- 1 g and that is not a place on the panel.
local function place(g)
  local v = (g + 1) / 2
  if v < 0 then v = 0 end
  if v > 1 then v = 1 end
  return math.floor(v * 4 + 0.5)
end

local lit = -1
local said = nil

-- One light and twenty-four dark ones: the panel is a level and not a lamp.
local function paint(cell)
  for light = 0, 24 do
    if light == cell then
      led.pixel(light, bubble, bubble, 0)
    else
      led.pixel(light, 0, 0, 0)
    end
  end
end

while true do
  local reading, err = imu.read()
  if not reading then
    console.print('tilt: the sensor said ' .. tostring(err and err.code))
    return
  end
  -- y is the row and x the column, so the bubble goes where gravity pulls.
  local cell = place(reading.ay) * 5 + place(reading.ax)
  if cell ~= lit then
    paint(cell)
    lit = cell
  end
  -- Said only when it changes: a level that prints four times a second buries
  -- whatever else the console has to say.
  local line = string.format('tilt %.2f %.2f g  %.1f C', reading.ax, reading.ay,
                             reading.celsius)
  if line ~= said then
    console.print(line)
    said = line
  end
  sys.delay(100):await()
end
