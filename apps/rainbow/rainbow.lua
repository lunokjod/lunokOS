-- rainbow: **the lights of the board as a rainbow that drifts** (0.179.18). The light is the whole face of these boards
-- —one on the Lite, a five by five panel on the Matrix— so what a page would be here is a hue for every light and a
-- phase that walks them (docs/50, docs/85).
--
-- **THE PANEL IS DERIVED FROM THE COUNT AND NOT WRITTEN DOWN**: `led.pixel` counts from zero in the order the strip is
-- wired and `led.caps().count` says how many lights there are, so the five by five is the square of that number and the
-- same app on the Lite's single light is one hue that breathes (docs/30).
--
-- **A CHANGE OF FIELD IS TWO FIELDS MIXED AND NOT TWO FRAMES CUT TOGETHER**: four fields take turns, and the one that
-- leaves and the one that arrives are mixed in hue and in level for two and a half seconds. The hue is mixed the short
-- way round the wheel —355 and 5 degrees are ten apart and not three hundred and fifty—, and that is what makes the
-- change a colour that travels instead of a panel that blinks (the owner: *«canvis de to suaus i moviment»*).
--
-- **A FRAME IS ONE TURN AND THE TURN ENDS IN A WAIT**: `sys.delay` is a promise and `await` gives the coroutine back to
-- the kernel, which is who paints the strip at the end of a turn and only when the frame changed (docs/30, docs/20).
-- Twenty-five lights of arithmetic fit in a turn with room to spare —the budget is twenty thousand instructions—, so
-- what a person sees is a whole panel and never half of one (docs/20, docs/75).
--
-- **THE LEVEL IS THE CEILING OF THE PANEL AND NOT 255**: `led.caps().max` is **twenty** on the Matrix, the maker's
-- number for a closed case of twenty-five LEDs, and a ramp that went to 255 would be a ramp the system cuts at twenty
-- without telling the app (docs/30). **And the panel runs at that ceiling** (0.179.20): the first version kept the
-- levels between a third and a whole for the sake of a soft rim, and on the board that reads as a pane of dark dots —a
-- light at six of twenty is a light nobody sees—. A demo has to be as bright as the board allows, so what moves is the
-- **hue** and the level is a whole except where a field is *about* the level: the rim of `iris`, and the band of
-- `rain`, which goes a third down and no more (the owner: *«no veig animació ni tampoc que hagis pujat el brillo al
-- tope»*).
--
-- **AND IT SAYS HELLO**: a quarter of a second of the whole panel at the ceiling before the first field, because a light
-- that comes on blue and then starts moving is a light a person can tell apart from one that never got the turn (docs/30).
--
-- **IT IS NOT AN APP OF PROVA**: it draws its own icon, because it is an app a person runs to look at the board
-- (docs/40).

local caps = led.caps()
if caps.count == 0 then
  console.print('rainbow: this board has no light')
  return
end

-- **Twenty-five frames a second**: a hue that moves at this rate reads as a colour travelling and not as a set of
-- steps, and a strip of twenty-five lights takes a frame in under a millisecond (docs/50).
local kFrameMs = 40
-- **A field stays seven seconds and takes two and a half to become the next one**: the hold is what lets a person read
-- the shape of it, and the fade is what makes the change kind (docs/30).
local kHoldMs = 7000
local kFadeMs = 2600
local kCycleMs = kHoldMs + kFadeMs
-- **The hello**: a quarter of a second of the whole panel at the ceiling when the app takes the light, and it is short
-- enough that nobody waits for it (docs/30).
local kHelloMs = 260

local count = caps.count
local top = caps.max
if top < 1 then top = 1 end
if top > 255 then top = 255 end

-- **The square the strip is**: five by five on the Matrix, one on the Lite, and whatever a board of nine lights would
-- be. `span` is the distance from the first row to the last, and it is one for a single light so that no field divides
-- by zero (docs/30).
local side = math.floor(math.sqrt(count) + 0.5)
if side < 1 then side = 1 end
local span = side - 1
if span < 1 then span = 1 end
local half = (side - 1) / 2
local middle = 1

-- **Where every light is, worked out once**: the column, the row, the radius out of the middle and the angle around it
-- as a fraction of a turn. A field is a function of these and of the clock, so a field is cheap and a frame is a walk
-- over the strip (docs/30).
local g_x = {}
local g_y = {}
local g_r = {}
local g_a = {}
for i = 0, count - 1 do
  local x = i % side
  local y = math.floor(i / side)
  local dx = x - half
  local dy = y - half
  local r = math.sqrt(dx * dx + dy * dy)
  if r > middle then middle = r end
  g_x[i] = x
  g_y[i] = y
  g_r[i] = r
  if r > 0 then
    g_a[i] = math.atan(dy, dx) / (2 * math.pi) + 0.5
  else
    g_a[i] = 0
  end
end

local function frac(v)
  return v - math.floor(v)
end

-- **The four fields**: a hue and a level for every light from where it is and how much time has passed, and no state
-- kept anywhere. The same millisecond always paints the same panel, which is what makes a capture of it a fact
-- (docs/30, docs/85).

-- The bands: the wheel laid diagonally across the panel and sliding towards the corner, at the ceiling of the board.
local function bands(i, t)
  return frac((g_x[i] + g_y[i]) * 0.11 + t * 0.085), 1
end

-- The iris: a hue per radius out of the middle, and a rim that goes a little soft —a third off and not two thirds, so
-- that the ring is a shape and not a hole (docs/30).
local function iris(i, t)
  local r = g_r[i] / middle
  return frac(r * 0.62 + t * 0.10), 0.58 + 0.42 * (1 - r * r * 0.9)
end

-- The spiral: the hue turns with the angle and the radius at once, so the bands twist around the middle.
local function spiral(i, t)
  local r = g_r[i] / middle
  return frac(g_a[i] + r * 0.55 - t * 0.07), 0.64 + 0.36 * (1 - r * 0.55)
end

-- The rain: one hue for the panel and a band of light that travels down it, which is movement without a new colour.
-- The crest is squared so it reads as a band and not as a panel going up and down, and the floor is **a third** of the
-- ceiling —a tenth was invisible on the board— so the lights the crest has left behind are still lights (docs/30).
local function rain(i, t)
  local crest = 0.5 + 0.5 * math.sin((g_y[i] / span - t * 0.45) * 2 * math.pi)
  return frac(0.55 + g_x[i] * 0.07 - t * 0.075), 0.34 + 0.66 * crest * crest
end

local g_fields = {bands, iris, spiral, rain}
local kFields = 4
local kNames = {'bands', 'iris', 'spiral', 'rain'}

-- **The wheel in six ramps of the same length**: a hue is a position around it and the three channels are the three
-- things that rise, fall and wait as the position moves. `v` is a level in the panel's own numbers, so the wheel never
-- has to know what the ceiling of the board is (docs/30).
local function wheel(h, v)
  local at = frac(h) * 6
  local sector = math.floor(at)
  local f = at - sector
  local rise = math.floor(v * f + 0.5)
  local fall = math.floor(v * (1 - f) + 0.5)
  if sector < 1 then return v, rise, 0 end
  if sector < 2 then return fall, v, 0 end
  if sector < 3 then return 0, v, rise end
  if sector < 4 then return 0, fall, v end
  if sector < 5 then return rise, 0, v end
  return v, 0, fall
end

-- **A press jumps to the boundary between two fields**, which is the one place where the frame has no mix in it: after
-- it the panel shows the next field whole, so the change a person asked for is the change they see at once (docs/30).
local started = sys.time_ms()
local bias = 0
local shown = 0
-- **The presses that have arrived and the frame has not used yet**: the hook is called between turns, with this
-- coroutine parked, so it cannot paint; it counts and the loop does the rest (docs/30).
local presses = 0

-- **A press is an edge, and the hook is the edge** (0.179.21): the loop paints the panel twenty-five times a second, so a
-- finger on the button for less than a frame would be a press nobody sees if the loop asked the level. `on_button` is
-- called with the coroutine parked and it misses nothing —it is the second of the three ways docs/30 gives, and the one
-- for a press that happens between two frames—.
function on_button(kind)
  if kind == 'press' then presses = presses + 1 end
end

console.print(string.format('rainbow: %d light(s) in %d by %d, %d fields of %d s, the button takes the next one',
                            count, side, side, kFields, math.floor(kCycleMs / 1000)))

-- **The hello**: the whole panel at the ceiling, once, so that a person sees the app take the light before the first
-- field has moved at all. On the board this is the difference between an app that works and an app that nobody can tell
-- from a broken one (docs/30).
for i = 0, count - 1 do
  led.pixel(i, top, top, top)
end
sys.delay(kHelloMs):await()

while true do
  local now = sys.time_ms()
  if presses > 0 then
    -- Every press the hook counted moves the fields on by one, and the first of them lands on the boundary so that the
    -- panel shows the next field whole and not half of a mix (docs/30).
    bias = bias + (kCycleMs - ((now - started + bias) % kCycleMs)) + (presses - 1) * kCycleMs
    presses = 0
  end

  local t = now - started + bias
  local index = math.floor(t / kCycleMs) % kFields + 1
  local phase = t % kCycleMs
  local mix = 0
  if phase > kHoldMs then
    mix = (phase - kHoldMs) / kFadeMs
    mix = mix * mix * (3 - 2 * mix)
  end
  if index ~= shown then
    shown = index
    console.print(string.format('[rainbow] field %d of %d: %s', index, kFields, kNames[index]))
  end

  local here = g_fields[index]
  local following = g_fields[index % kFields + 1]
  local seconds = t / 1000

  for i = 0, count - 1 do
    local h1, level = here(i, seconds)
    local h = h1
    if mix > 0 then
      local h2, l2 = following(i, seconds)
      local away = h2 - h1
      if away > 0.5 then
        away = away - 1
      elseif away < -0.5 then
        away = away + 1
      end
      h = frac(h1 + away * mix)
      level = level + (l2 - level) * mix
    end
    local v = math.floor(level * top + 0.5)
    if v < 0 then v = 0 elseif v > top then v = top end
    local r, g, b = wheel(h, v)
    led.pixel(i, r, g, b)
  end

  local spent = sys.time_ms() - now
  local wait = kFrameMs - spent
  if wait < 1 then wait = 1 end
  sys.delay(wait):await()
end
