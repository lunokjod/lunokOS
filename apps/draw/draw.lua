-- draw: a finger paint (docs/30, docs/50).
--
-- What the touch panel gives is a frame of **points** and what the system makes of them is **gestures**,
-- and what a person wants to see is that the finger lands where it looks like it lands and that the
-- trail follows. So this app paints where the finger is, with the quality a finger deserves —the quick
-- one, because a page that takes half a second per point does not follow anything— and cleans the page
-- with the slow one when the last tap lands in the box at the bottom.
--
--   run draw           it stops by itself after twenty-five seconds, so a script can run it
--
-- Three taps of the system key close it at any time, which is what the system does with every app. A
-- tap inside the box cleans the page and does not count as one of the three (docs/30).
local caps = sys.caps()
if not caps.touch or not caps.display then
  console.print('this board needs a touch panel and a glass for this')
  return
end

local view = ui.caps()
local glass = touch.caps()
-- The quick quality for the trail and the slow one for the page: a finger moves, and a clean is the only
-- thing that takes the trace of a long stroke away (docs/50).
local kQuick = 'du'
local kFine = 'gc16'
-- How long it runs before it ends on its own, and how often it asks the panel for a paint: the ink of a
-- `du` takes about a tenth of a second, so painting every turn would queue paints behind the glass
-- (docs/50, docs/85).
local kSeconds = 25
local kPresentMs = 40

ui.title('draw')

local box = {x = view.width - 140, y = view.height - 70, w = 120, h = 50}

ui.clear(15, kFine)
ui.text(24, 24, 'draw: the trail follows the finger', 2, 0, kFine)
ui.text(24, 66, glass.width .. ' x ' .. glass.height .. ', ' .. glass.points .. ' fingers', 1, 0,
  kFine)
ui.frame(box.x, box.y, box.w, box.h, 0, kFine)
ui.text(box.x + 14, box.y + 18, 'clean', 1, 0, kFine)
if not ui.present() then
  console.print('the panel did not take the page')
  return
end
while not ui.idle() do
  sys.delay(10):await()
end

local function clean_inside(x, y)
  return x >= box.x and x < box.x + box.w and y >= box.y and y < box.y + box.h
end

local function repaint(mode)
  local started = sys.time_ms()
  if not ui.present(mode) then return -1 end
  while not ui.idle() do
    sys.delay(10):await()
  end
  return sys.time_ms() - started
end

console.print(string.format('drawing on %s (%s): touch the glass, and tap the box to clean', sys.board,
  sys.emulator and 'bench' or 'board'))

-- The state of the trail: the last point, which is what a line starts from, and how much was painted.
local last_x = nil
local last_y = nil
local strokes = 0
local paints = 0
local started = sys.time_ms()
local last_present = started

while sys.time_ms() - started < kSeconds * 1000 do
  local fingers = touch.read()
  local drew = false
  if #fingers > 0 then
    -- **The first finger of the frame** and not both: two fingers on a paint are a gesture and not a
    -- second colour, and a two-finger gesture is reported below (docs/30).
    local finger = fingers[1]
    if last_x then
      ui.line(last_x, last_y, finger.x, finger.y, 0, kQuick)
    else
      ui.disc(finger.x, finger.y, 3, 0, kQuick)
      strokes = strokes + 1
      -- One line per stroke and not per frame: a frame arrives every twenty milliseconds and a line per
      -- frame is a console nobody reads (docs/85).
      console.print(string.format('stroke %d at %d,%d of the view (the chip said %d,%d), contact %d',
        strokes, finger.x, finger.y, finger.x, finger.y, finger.size))
    end
    last_x, last_y = finger.x, finger.y
    drew = true
  elseif last_x then
    -- The finger left: the next one starts a new stroke instead of drawing a line across the page.
    last_x, last_y = nil, nil
  end

  -- **The gestures**, which is what the system made of the points: they are taken once each, and a tap
  -- in the box is the clean (docs/30).
  local gesture = touch.gesture()
  while gesture do
    if gesture.kind == 'tap' and clean_inside(gesture.x, gesture.y) then
      local took = repaint(kFine)
      console.print(string.format('cleaned with gc16: %d ms', took))
    elseif gesture.kind == 'tap' then
      ui.disc(gesture.x, gesture.y, 6, 8, kQuick)
      console.print(string.format('tap at %d,%d', gesture.x, gesture.y))
    else
      console.print(string.format('%s at %d,%d after %d ms', gesture.kind, gesture.x, gesture.y,
        gesture.held_ms))
    end
    gesture = touch.gesture()
  end

  -- A paint for the trail, at most every `kPresentMs`, and never two of them queued on the glass.
  if drew and sys.time_ms() - last_present >= kPresentMs then
    if ui.present() then
      paints = paints + 1
    end
    last_present = sys.time_ms()
  end

  sys.delay(20):await()
end

-- What the session was, and one paint measured the honest way so the number means something (docs/95).
local took = repaint(kFine)
console.print(string.format('%d stroke(s), %d paint(s) asked, and the last clean took %d ms', strokes,
  paints, took))
